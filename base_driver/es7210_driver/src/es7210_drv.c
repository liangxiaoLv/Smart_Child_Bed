/*
 * es7210_drv — ES7210/ES7210L 独立驱动 (不依赖 esp_codec_dev)
 *
 * 基于顺芯原厂 HAL 寄存器配置, 使用 ESP-IDF 原生 I2C + I2S API。
 * 自动识别 ES7210 / ES7210L 芯片版本。
 */

#include "es7210_drv.h"
#include "pin_map.h"

#include "i2c_driver.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "es7210_drv";

/* [临时] 设为 1 使用原厂寄存器序列, 0 还原 */
#define ES7210_USE_ALT_PROBE  1

/* ═══════════════════════════════════════════════════════════════
 * ES7210 寄存器地址
 * ═══════════════════════════════════════════════════════════════ */
#define REG00_RESET              0x00
#define REG01_CLK_ON             0x01
#define REG02_MAINCLK            0x02
#define REG03_MASTER_CLK         0x03
#define REG04_LRCK_DIVH          0x04
#define REG05_LRCK_DIVL          0x05
#define REG06_POWER_DOWN         0x06
#define REG07_OSR                0x07
#define REG08_MODE_CONFIG        0x08
#define REG09_TIME_CTRL0         0x09
#define REG0A_TIME_CTRL1         0x0A
#define REG0B                     0x0B
#define REG0D                     0x0D
#define REG0E_DMIC_FREQ          0x0E
#define REG0F                     0x0F
#define REG10_DMIC_CONFIG        0x10
#define REG11_ADCFMT             0x11
#define REG12_INVERT_TDM         0x12
#define REG14_ADC34_MUTE         0x14
#define REG15_ADC12_MUTE         0x15
#define REG1B_ADC4_VOL           0x1B
#define REG1C_ADC3_VOL           0x1C
#define REG1D_ADC2_VOL           0x1D
#define REG1E_ADC1_VOL           0x1E
#define REG20_ADC34_CROSS        0x20
#define REG21_ADC34_INVERT       0x21
#define REG22_ADC12_CROSS        0x22
#define REG23_ADC12_INVERT       0x23
#define REG3F_CHIP_ID            0x3F
#define REG40_ANALOG             0x40
#define REG41_MICBIAS12          0x41
#define REG42_MICBIAS34          0x42
#define REG43_PGA1_GAIN          0x43
#define REG44_PGA2_GAIN          0x44
#define REG45_PGA3_GAIN          0x45
#define REG46_PGA4_GAIN          0x46
#define REG47_MIC1_POWER         0x47
#define REG48_MIC2_POWER         0x48
#define REG49_MIC3_POWER         0x49
#define REG4A_MIC4_POWER         0x4A
#define REG4B_MIC12_POWER        0x4B
#define REG4C_MIC34_POWER        0x4C

/* ═══════════════════════════════════════════════════════════════
 * 内部结构
 * ═══════════════════════════════════════════════════════════════ */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    i2s_chan_handle_t       tx_chan;
    i2s_chan_handle_t       rx_chan;
    uint32_t                sample_rate;
    uint8_t                 mic_count;      /* 有效 MIC 通道数 */
    uint8_t                 mic_channels[4]; /* 已使能的通道号 (0-based) */
    uint8_t                 total_slots;    /* I2S 总 slot 数 */
    bool                    is_es7210l;     /* 芯片版本 */
} es7210_drv_t;

/* ═══════════════════════════════════════════════════════════════
 * I2C 寄存器读写
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t write_reg(es7210_drv_t *drv, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2cDriver_write(drv->i2c_dev, buf, 2, 100);
}

static esp_err_t read_reg(es7210_drv_t *drv, uint8_t reg, uint8_t *val)
{
    return i2cDriver_writeRead(drv->i2c_dev, &reg, 1, val, 1, 100);
}

/* ═══════════════════════════════════════════════════════════════
 * MIC 通道解析
 * ═══════════════════════════════════════════════════════════════ */
static int parse_mic_channels(uint8_t mask, uint8_t *channels)
{
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            channels[count++] = i;
        }
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
 * I2S 初始化
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t i2s_init(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    i2s_chan_config_t chan_cfg = {
        .id                  = I2S_NUM_0,
        .role                = I2S_ROLE_MASTER,
        .dma_desc_num        = 3,
        .dma_frame_num       = 240,
        .auto_clear_after_cb  = true,
        .auto_clear_before_cb = false,
        .intr_priority       = 0,
    };

    esp_err_t ret = i2s_new_channel(&chan_cfg, &drv->tx_chan, &drv->rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel fail: %s", esp_err_to_name(ret));
        return ret;
    }

    int slots = cfg->total_slots;
    if (slots == 0) slots = 2;  /* 默认立体声 */

    if (slots <= 2) {
        /* ── STD 模式 (2 通道立体声) ───────────────────────── */
        i2s_std_config_t std_cfg = {
            .clk_cfg = {
                .sample_rate_hz = cfg->sample_rate,
                .clk_src        = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
                .slot_mode      = I2S_SLOT_MODE_STEREO,
                .slot_mask      = I2S_STD_SLOT_BOTH,
                .ws_width       = I2S_DATA_BIT_WIDTH_16BIT,
                .ws_pol         = false,
                .bit_shift      = true,
                .left_align     = false,
                .big_endian     = false,
                .bit_order_lsb  = false,
            },
            .gpio_cfg = {
                .mclk = cfg->mclk_io,
                .bclk = cfg->bclk_io,
                .ws   = cfg->ws_io,
                .dout = I2S_GPIO_UNUSED,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv   = false,
                },
            },
        };
        ret = i2s_channel_init_std_mode(drv->tx_chan, &std_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_std_mode(TX) fail: %s", esp_err_to_name(ret));
            return ret;
        }

        i2s_std_config_t rx_cfg = std_cfg;
        rx_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        rx_cfg.gpio_cfg.bclk = I2S_GPIO_UNUSED;
        rx_cfg.gpio_cfg.ws   = I2S_GPIO_UNUSED;
        rx_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
        rx_cfg.gpio_cfg.din  = cfg->din_io;

        ret = i2s_channel_init_std_mode(drv->rx_chan, &rx_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_std_mode(RX) fail: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        /* ── TDM 模式 (多通道) ──────────────────────────────── */
        i2s_tdm_slot_mask_t mask = 0;
        for (int i = 0; i < slots; i++) mask |= (1 << i);

        i2s_tdm_config_t tdm_cfg = {
            .clk_cfg = {
                .sample_rate_hz = cfg->sample_rate,
                .clk_src        = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
                .slot_mode      = I2S_SLOT_MODE_MONO,
                .slot_mask      = mask,
                .ws_width       = 16,
                .ws_pol         = false,
                .bit_shift      = true,
                .left_align     = false,
                .big_endian     = false,
                .bit_order_lsb  = false,
                .skip_mask      = false,
                .total_slot     = slots,
            },
            .gpio_cfg = {
                .mclk = cfg->mclk_io,
                .bclk = cfg->bclk_io,
                .ws   = cfg->ws_io,
                .dout = I2S_GPIO_UNUSED,
                .din  = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv   = false,
                },
            },
        };
        ret = i2s_channel_init_tdm_mode(drv->tx_chan, &tdm_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_tdm_mode(TX) fail: %s", esp_err_to_name(ret));
            return ret;
        }

        tdm_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        tdm_cfg.gpio_cfg.bclk = I2S_GPIO_UNUSED;
        tdm_cfg.gpio_cfg.ws   = I2S_GPIO_UNUSED;
        tdm_cfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
        tdm_cfg.gpio_cfg.din  = cfg->din_io;

        ret = i2s_channel_init_tdm_mode(drv->rx_chan, &tdm_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_tdm_mode(RX) fail: %s", esp_err_to_name(ret));
            return ret;
        }
    }

    /* TX+RX 使能, 产生 MCLK 供 ES7210 使用 */
    ret = i2s_channel_enable(drv->tx_chan);
    if (ret != ESP_OK) return ret;
    ret = i2s_channel_enable(drv->rx_chan);
    return ret;
}

/* ═══════════════════════════════════════════════════════════════
 * ES7210 探头初始化 (参考 Everest_probe)
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t es7210_probe(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    esp_err_t ret;

    /* 1. 复位 */
    ret  = write_reg(drv, REG00_RESET, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(5));
    ret |= write_reg(drv, REG00_RESET, 0x32);
    if (ret) return ESP_FAIL;

    /* 2. 时间控制 */
    ret  = write_reg(drv, REG09_TIME_CTRL0, 0x30);
    ret |= write_reg(drv, REG0A_TIME_CTRL1, 0x20);
    if (ret) return ESP_FAIL;

    /* 3. HPF + Cross + Invert */
    ret  = write_reg(drv, REG20_ADC34_CROSS, 0x0A);
    ret |= write_reg(drv, REG21_ADC34_INVERT, 0x2A);
    ret |= write_reg(drv, REG22_ADC12_CROSS, 0x0A);
    ret |= write_reg(drv, REG23_ADC12_INVERT, 0x2A);
    if (ret) return ESP_FAIL;

    /* 4. 时钟: MCLK=4.096MHz, LRCK=16kHz, Ratio=256 */
    ret  = write_reg(drv, REG02_MAINCLK,    0xC1);
    ret |= write_reg(drv, REG03_MASTER_CLK, 0x04);
    ret |= write_reg(drv, REG04_LRCK_DIVH,  0x01);   /* Ratio high */
    ret |= write_reg(drv, REG05_LRCK_DIVL,  0x00);   /* Ratio low */
    ret |= write_reg(drv, REG07_OSR,        0x20);
    if (ret) return ESP_FAIL;

    /* 5. Mode: Slave, no TDM, no EQ, BCLK no-invert */
    ret = write_reg(drv, REG08_MODE_CONFIG, 0x00);
    if (ret) return ESP_FAIL;

    /* 6. Audio format: S16_LE, I2S standard */
    ret = write_reg(drv, REG11_ADCFMT, 0x60);
    if (ret) return ESP_FAIL;

    /* 7. TDM/SDOUT: no TDM, SDOUT non-tri-state */
    ret = write_reg(drv, REG12_INVERT_TDM, 0x04);
    if (ret) return ESP_FAIL;

    /* 8. 解除静音 */
    ret  = write_reg(drv, REG14_ADC34_MUTE, 0x3C);
    ret |= write_reg(drv, REG15_ADC12_MUTE, 0x3C);
    if (ret) return ESP_FAIL;

    /* 9. DMIC: 禁用 */
    ret = write_reg(drv, REG10_DMIC_CONFIG, 0x00);
    if (ret) return ESP_FAIL;

    /* 10. MIC bias: 关闭 (板载外部 LDO 提供 2.8V) */
    ret  = write_reg(drv, REG41_MICBIAS12, 0x00);
    ret |= write_reg(drv, REG42_MICBIAS34, 0x00);
    if (ret) return ESP_FAIL;

    /* 11. CHIP_ID 检测 (ES7210 vs ES7210L) */
    uint8_t chip_id;
    ret = read_reg(drv, REG3F_CHIP_ID, &chip_id);
    if (ret) return ESP_FAIL;
    drv->is_es7210l = ((chip_id >> 4) != 0x00);
    ESP_LOGI(TAG, "检测到 %s (CHIP_ID=0x%02X)",
             drv->is_es7210l ? "ES7210L" : "ES7210", chip_id);

    /* 12. MIC 电源 (按芯片版本区分配置) */
    uint8_t mic_pwr_val, mic_pwr_mask;
    if (drv->is_es7210l) {
        mic_pwr_val  = 0x26;   /* ES7210L */
        mic_pwr_mask = 0x06;
    } else {
        mic_pwr_val  = 0x3E;   /* ES7210 */
        mic_pwr_mask = 0x1E;
    }
    ret  = write_reg(drv, REG47_MIC1_POWER, mic_pwr_val);
    ret |= write_reg(drv, REG48_MIC2_POWER, mic_pwr_mask);
    ret |= write_reg(drv, REG49_MIC3_POWER, mic_pwr_val);
    ret |= write_reg(drv, REG4A_MIC4_POWER, mic_pwr_mask);
    if (ret) return ESP_FAIL;

    /* 13. PGA 增益: 按 mic_mask 使能的通道配置 */
    uint8_t pga_val = 0x10 | (cfg->pga_gain & 0x0F);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC1) write_reg(drv, REG43_PGA1_GAIN, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC2) write_reg(drv, REG44_PGA2_GAIN, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC3) write_reg(drv, REG45_PGA3_GAIN, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC4) write_reg(drv, REG46_PGA4_GAIN, pga_val);

    /* 14. 数字音量: 191 = 0dB (按使能通道) */
    for (int i = 0; i < drv->mic_count; i++) {
        uint8_t vol_reg = REG1E_ADC1_VOL - drv->mic_channels[i];  /* 0x1E→ADC1, 0x1D→ADC2, ... */
        write_reg(drv, vol_reg, 191);
    }

    /* 15. DMIC freq + 其他时钟辅助寄存器 */
    write_reg(drv, REG0E_DMIC_FREQ, 0x0A);
    write_reg(drv, REG0D, 0x09);
    write_reg(drv, REG0F, 0xFF);

    ESP_LOGI(TAG, "ES7210 probe 完成, mic_mask=0x%02X, pga_gain=%d",
             cfg->mic_mask, cfg->pga_gain);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * [临时] 替代探头配置 — 寄存器序列来自原厂参考
 *
 * 切换方式: 在 es7210_drv_init 中将 es7210_probe 替换为
 *          es7210_probe_alt 即可
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t es7210_probe_alt(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    esp_err_t ret;

    /* ─ 寄存器序列 ──────────────────────────────────────────── */
    ret  = write_reg(drv, 0x00, 0xFF);   /* :w00FF@ */
    ret |= write_reg(drv, 0x00, 0x32);   /* :w0032@ */
    if (ret) return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(5));

    ret  = write_reg(drv, 0x09, 0x30);   /* :w0930@ */
    ret |= write_reg(drv, 0x0A, 0x30);   /* :w0A30@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x23, 0x2A);   /* :w232A@ */
    ret |= write_reg(drv, 0x22, 0x0A);   /* :w220A@ */
    ret |= write_reg(drv, 0x21, 0x2A);   /* :w212A@ */
    ret |= write_reg(drv, 0x20, 0x0A);   /* :w200A@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x40, 0xAA);   /* :w40AA@ */
    ret |= write_reg(drv, 0x41, 0x70);   /* :w4170@ */
    ret |= write_reg(drv, 0x42, 0x70);   /* :w4270@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x43, 0x18);   /* :w4318@ */
    ret |= write_reg(drv, 0x44, 0x18);   /* :w4418@ */
    ret |= write_reg(drv, 0x45, 0x18);   /* :w4518@ */
    ret |= write_reg(drv, 0x46, 0x18);   /* :w4618@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x47, 0x08);   /* :w4708@ */
    ret |= write_reg(drv, 0x48, 0x08);   /* :w4808@ */
    ret |= write_reg(drv, 0x49, 0x08);   /* :w4908@ */
    ret |= write_reg(drv, 0x4A, 0x08);   /* :w4A08@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x02, 0xC1);   /* :w02C1@ */
    ret |= write_reg(drv, 0x07, 0x20);   /* :w0720@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x4B, 0x0F);   /* :w4B0F@ */
    ret |= write_reg(drv, 0x4C, 0x0F);   /* :w4C0F@ */
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, 0x00, 0x71);   /* :w0071@ */
    ret |= write_reg(drv, 0x00, 0x41);   /* :w0041@ */
    if (ret) return ESP_FAIL;

    /* ─ 补充原 probe 中配置的寄存器(原厂序列未包含的) ─────── */
    /* 时钟分频: 原厂序列省略了 REG03/04/05, 沿用当前配置 */
    write_reg(drv, REG03_MASTER_CLK, 0x04);
    write_reg(drv, REG04_LRCK_DIVH,  0x01);
    write_reg(drv, REG05_LRCK_DIVL,  0x00);

    /* 模式 & 音频格式: TDM 模式 */
    if (cfg->total_slots > 2) {
        write_reg(drv, REG08_MODE_CONFIG, 0x80);   /* Slave, TDM enable */
        write_reg(drv, REG12_INVERT_TDM, 0x0C);    /* TDM 4-slot, SDOUT on */
    } else {
        write_reg(drv, REG08_MODE_CONFIG, 0x00);   /* Slave, no TDM */
        write_reg(drv, REG12_INVERT_TDM, 0x00);    /* SDOUT non-tri-state */
    }
    write_reg(drv, REG11_ADCFMT,     0x60);    /* I2S, 16-bit */

    /* 解除静音 */
    write_reg(drv, REG14_ADC34_MUTE, 0x3C);
    write_reg(drv, REG15_ADC12_MUTE, 0x3C);

    /* DMIC 禁用 + 辅助寄存器 */
    write_reg(drv, REG10_DMIC_CONFIG, 0x00);
    write_reg(drv, REG0E_DMIC_FREQ,   0x0A);
    write_reg(drv, REG0D,             0x09);
    write_reg(drv, REG0F,             0xFF);

    /* 数字音量: 191 = 0dB */
    write_reg(drv, REG1E_ADC1_VOL, 191);
    write_reg(drv, REG1D_ADC2_VOL, 191);
    write_reg(drv, REG1C_ADC3_VOL, 191);
    write_reg(drv, REG1B_ADC4_VOL, 191);

    /* 时钟辅助 */
    write_reg(drv, REG0B, 0x02);

    ESP_LOGI(TAG, "ES7210 probe_alt 完成 (原厂序列 + 补充配置)");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * ES7210 上电 (参考 Everest_set_bias_on)
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t es7210_power_on(es7210_drv_t *drv)
{
    esp_err_t ret;

    ret  = write_reg(drv, REG06_POWER_DOWN, 0x00);
    ret |= write_reg(drv, REG01_CLK_ON,    0x20);
    ret |= write_reg(drv, REG40_ANALOG,    0x42);
    ret |= write_reg(drv, REG0B,           0x02);
    ret |= write_reg(drv, REG4B_MIC12_POWER, 0x0F);
    ret |= write_reg(drv, REG4C_MIC34_POWER, 0x0F);
    if (ret) return ESP_FAIL;

    /* ADC 使能序列: 0x31 → delay → 0x01 */
    ret  = write_reg(drv, REG00_RESET, 0x31);
    vTaskDelay(pdMS_TO_TICKS(5));
    ret |= write_reg(drv, REG00_RESET, 0x01);
    if (ret) return ESP_FAIL;

    ESP_LOGI(TAG, "ES7210 上电完成");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * 公共 API
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t es7210_drv_init(const es7210_drv_config_t *cfg, es7210_drv_handle_t *handle)
{
    if (!cfg || !handle || !cfg->i2c_bus) return ESP_ERR_INVALID_ARG;

    es7210_drv_t *drv = calloc(1, sizeof(*drv));
    if (!drv) return ESP_ERR_NO_MEM;

    drv->sample_rate = cfg->sample_rate;
    drv->total_slots = cfg->total_slots ? cfg->total_slots : 2;
    drv->mic_count   = parse_mic_channels(cfg->mic_mask, drv->mic_channels);

    esp_err_t ret;

    /* ── 1. I2C 设备挂载 ─────────────────────────────────────── */
    ret = i2cDriver_addDevice(cfg->i2c_bus, 0x40, 100000, &drv->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device fail");
        goto fail_i2c;
    }

    /* ── 2. I2S 初始化 + 使能 (先产生 MCLK) ──────────────────── */
    ret = i2s_init(drv, cfg);
    if (ret != ESP_OK) goto fail_i2s;

    /* ── 3. ES7210 探头 + 配置寄存器 ─────────────────────────── */
#if ES7210_USE_ALT_PROBE
    ret = es7210_probe_alt(drv, cfg);
#else
    ret = es7210_probe(drv, cfg);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES7210 probe fail");
        goto fail_probe;
    }

    /* ── 4. 上电 ────────────────────────────────────────────── */
    ret = es7210_power_on(drv);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES7210 power-on fail");
        goto fail_probe;
    }

    ESP_LOGI(TAG, "初始化完成: SR=%"PRIu32"Hz, mics=%d, is_es7210l=%d",
             cfg->sample_rate, drv->mic_count, drv->is_es7210l);

    *handle = drv;
    return ESP_OK;

fail_probe:
    i2s_channel_disable(drv->rx_chan);
    i2s_channel_disable(drv->tx_chan);
    i2s_del_channel(drv->rx_chan);     /* 全双工通道对, 删除RX即自动清理TX */
fail_i2s:
    i2cDriver_removeDevice(drv->i2c_dev);
fail_i2c:
    free(drv);
    return ret;
}

int es7210_drv_read(es7210_drv_handle_t handle, int16_t *buf, int samples)
{
    if (!handle || !buf || samples <= 0) return -1;

    es7210_drv_t *drv = (es7210_drv_t *)handle;
    int slots = drv->total_slots ? drv->total_slots : 2;
    size_t bytes_read = 0;
    size_t bytes_req  = samples * sizeof(int16_t) * slots;

    esp_err_t ret = i2s_channel_read(drv->rx_chan, buf, bytes_req, &bytes_read, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
        return -1;
    }
    return (int)(bytes_read / (sizeof(int16_t) * slots));
}

#define DUMP_REG(name) do { \
    read_reg(drv, name, &val); \
    ESP_LOGI(TAG, "  %-18s (0x%02X) = 0x%02X", #name, name, val); \
} while(0)

void es7210_drv_dump_regs(es7210_drv_handle_t handle)
{
    if (!handle) return;

    es7210_drv_t *drv = (es7210_drv_t *)handle;
    uint8_t val;

    ESP_LOGI(TAG, "══════════ ES7210 寄存器 DUMP ══════════");

    ESP_LOGI(TAG, "── 系统 ──");
    DUMP_REG(REG00_RESET);
    DUMP_REG(REG01_CLK_ON);
    DUMP_REG(REG3F_CHIP_ID);

    ESP_LOGI(TAG, "── 时钟 ──");
    DUMP_REG(REG02_MAINCLK);
    DUMP_REG(REG03_MASTER_CLK);
    DUMP_REG(REG04_LRCK_DIVH);
    DUMP_REG(REG05_LRCK_DIVL);
    DUMP_REG(REG07_OSR);

    ESP_LOGI(TAG, "── 模式 ──");
    DUMP_REG(REG08_MODE_CONFIG);
    DUMP_REG(REG09_TIME_CTRL0);
    DUMP_REG(REG0A_TIME_CTRL1);
    DUMP_REG(REG0B);
    DUMP_REG(REG0D);
    DUMP_REG(REG0E_DMIC_FREQ);
    DUMP_REG(REG0F);

    ESP_LOGI(TAG, "── 音频格式 ──");
    DUMP_REG(REG11_ADCFMT);
    DUMP_REG(REG12_INVERT_TDM);
    DUMP_REG(REG14_ADC34_MUTE);
    DUMP_REG(REG15_ADC12_MUTE);

    ESP_LOGI(TAG, "── 模拟前端 ──");
    DUMP_REG(REG40_ANALOG);
    DUMP_REG(REG41_MICBIAS12);
    DUMP_REG(REG42_MICBIAS34);

    ESP_LOGI(TAG, "── PGA 增益 ──");
    DUMP_REG(REG43_PGA1_GAIN);
    DUMP_REG(REG44_PGA2_GAIN);
    DUMP_REG(REG45_PGA3_GAIN);
    DUMP_REG(REG46_PGA4_GAIN);

    ESP_LOGI(TAG, "── MIC 电源 ──");
    DUMP_REG(REG47_MIC1_POWER);
    DUMP_REG(REG48_MIC2_POWER);
    DUMP_REG(REG49_MIC3_POWER);
    DUMP_REG(REG4A_MIC4_POWER);
    DUMP_REG(REG4B_MIC12_POWER);
    DUMP_REG(REG4C_MIC34_POWER);

    ESP_LOGI(TAG, "── 音量 & 信号路径 ──");
    DUMP_REG(REG1B_ADC4_VOL);
    DUMP_REG(REG1C_ADC3_VOL);
    DUMP_REG(REG1D_ADC2_VOL);
    DUMP_REG(REG1E_ADC1_VOL);
    DUMP_REG(REG20_ADC34_CROSS);
    DUMP_REG(REG21_ADC34_INVERT);
    DUMP_REG(REG22_ADC12_CROSS);
    DUMP_REG(REG23_ADC12_INVERT);

    ESP_LOGI(TAG, "── DMIC ──");
    DUMP_REG(REG10_DMIC_CONFIG);

    ESP_LOGI(TAG, "── 电源 ──");
    DUMP_REG(REG06_POWER_DOWN);

    ESP_LOGI(TAG, "══════════ DUMP 完成 ══════════");
}

esp_err_t es7210_drv_deinit(es7210_drv_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    es7210_drv_t *drv = (es7210_drv_t *)handle;

    /* 下电 ES7210 */
    write_reg(drv, REG06_POWER_DOWN, 0x00);
    write_reg(drv, REG4B_MIC12_POWER, 0xFF);
    write_reg(drv, REG4C_MIC34_POWER, 0xFF);
    write_reg(drv, REG40_ANALOG, 0x80);
    write_reg(drv, REG01_CLK_ON, 0x7F);
    write_reg(drv, REG06_POWER_DOWN, 0x07);

    /* 关闭 I2S */
    if (drv->rx_chan) {
        i2s_channel_disable(drv->rx_chan);
        i2s_del_channel(drv->rx_chan);
    }
    if (drv->tx_chan) {
        i2s_channel_disable(drv->tx_chan);
        i2s_del_channel(drv->tx_chan);
    }

    free(drv);
    return ESP_OK;
}
