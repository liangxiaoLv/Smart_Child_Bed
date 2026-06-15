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

/* ═══════════════════════════════════════════════════════════════
 * ES7210 寄存器地址 (已在 es7210_drv.h 中定义的直接使用, 此处仅
 * 保留 .h 中未提供的寄存器)
 * ═══════════════════════════════════════════════════════════════ */
#define ES7210_CHIP_ID_REG3F         0x3F
#define ES7210_DMIC_FREQ_REG0E       0x0E
#define ES7210_DMIC_CONFIG_REG10     0x10
#define ES7210_ADC12_MUTE_REG15      0x15
#define ES7210_REG0B                 0x0B
#define ES7210_REG0D                 0x0D
#define ES7210_REG0F                 0x0F

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

#define DUMP_REG(name) do { \
    read_reg(drv, name, &val); \
    ESP_LOGI(TAG, "  %-18s (0x%02X) = 0x%02X", #name, name, val); \
} while(0)

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
                .slot_mode      = I2S_SLOT_MODE_STEREO,
                .slot_mask      = mask,
                .ws_width       = 1,
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

static esp_err_t es7210_reset(es7210_drv_t *drv)
{
    esp_err_t ret;
    ret  = write_reg(drv, ES7210_RESET_REG00, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(10));  // 硬件复位只需要至少1ms，这里预留，delay 10ms 确保稳定
    ret |= write_reg(drv, ES7210_RESET_REG00, 0x0); // TODO: 先按照0x0配置试试
    return ret;
}


/* ═══════════════════════════════════════════════════════════════
 * ES7210 探头初始化 (参考 Everest_probe)
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t es7210_probe(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    esp_err_t ret;

    /* 1. 复位 */
    ret  = es7210_reset(drv);
    if (ret) return ESP_FAIL;

    /* 2. 主/从 时钟配置 从模式，时钟由esp32s3提供*/
    /* Mode: Slave, TDM I2S, no EQ, BCLK no-invert */
    ret = write_reg(drv, ES7210_MODE_CONFIG_REG08, 0x00);
    if (ret) return ESP_FAIL; 

     /* 3. 时钟: MCLK=4.096MHz, LRCK=16kHz, Ratio=256 */
    ret  = write_reg(drv, ES7210_MAINCLK_REG02,    0xC1);  /*将 MCLK 倍频 2 倍后送入 ADC 调制器*/
    ret |= write_reg(drv, ES7210_MASTER_CLK_REG03, 0x04);  /*时钟来源选 外部引脚 MCLK  将 MCLK 4 分频得到 BCLK = 1.024MHz*/
    /*reg04和reg05在ES7210 slave模式下不使用，
    在 Slave 模式下写入 256，作用是告诉芯片内部数字滤波器 BCLK 与 LRCK 的期望比例为 256:1
    */
    ret |= write_reg(drv, ES7210_LRCK_DIVH_REG04,  0x01);   /* Ratio high */
    ret |= write_reg(drv, ES7210_LRCK_DIVL_REG05,  0x00);   /* Ratio low */

    ret |= write_reg(drv, ES7210_OSR_REG07,        0x20);   /*配置ADC过采样率=32*/
    if (ret) return ESP_FAIL;

    /* 2. 时间控制 */
    ret  = write_reg(drv, ES7210_TIME_CONTROL0_REG09, 0x30);
    ret |= write_reg(drv, ES7210_TIME_CONTROL1_REG0A, 0x20);
    if (ret) return ESP_FAIL;

    /* 3. HPF + Cross + Invert */
    ret  = write_reg(drv, ES7210_ADC34_HPF2_REG20, 0x0A);
    ret |= write_reg(drv, ES7210_ADC34_HPF1_REG21, 0x2A);
    ret |= write_reg(drv, ES7210_ADC12_HPF2_REG22, 0x0A);
    ret |= write_reg(drv, ES7210_ADC12_HPF1_REG23, 0x2A);
    if (ret) return ESP_FAIL;

   

    

    /* 6. Audio format: S16_LE, I2S standard */
    ret = write_reg(drv, ES7210_SDP_INTERFACE1_REG11, 0x60);
    if (ret) return ESP_FAIL;

    /* 7. TDM/SDOUT: TDM I2S 模式, 所有通道到 SDOUT1 */
    ret = write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x02);
    if (ret) return ESP_FAIL;

    /* 8. 解除静音 (0x00 = all unmuted) */
    ret  = write_reg(drv, ES7210_ADC34_MUTERANGE_REG14, 0x00);
    ret |= write_reg(drv, ES7210_ADC12_MUTE_REG15, 0x00);
    if (ret) return ESP_FAIL;

    /* 9. DMIC: 禁用 */
    ret = write_reg(drv, ES7210_DMIC_CONFIG_REG10, 0x00);
    if (ret) return ESP_FAIL;

    /* 10. MIC bias: 关闭 (板载外部 LDO 提供 2.8V) */
    ret  = write_reg(drv, ES7210_MIC12_BIAS_REG41, 0x00);
    ret |= write_reg(drv, ES7210_MIC34_BIAS_REG42, 0x00);
    if (ret) return ESP_FAIL;

    /* 11. CHIP_ID 检测 (ES7210 vs ES7210L) */
    uint8_t chip_id;
    ret = read_reg(drv, ES7210_CHIP_ID_REG3F, &chip_id);
    if (ret) return ESP_FAIL;
    drv->is_es7210l = ((chip_id >> 4) != 0x00);
    ESP_LOGI(TAG, "检测到 %s (CHIP_ID=0x%02X)",
             drv->is_es7210l ? "ES7210L" : "ES7210", chip_id);

    /* 12. MIC 电源 (按芯片版本区分配置) */
    uint8_t mic_pwr_val, mic_pwr_mask;
    mic_pwr_val  = 0x3E;   /* ES7210 */
    mic_pwr_mask = 0x1E;
    ret  = write_reg(drv, ES7210_MIC1_POWER_REG47, mic_pwr_val);
    ret |= write_reg(drv, ES7210_MIC2_POWER_REG48, mic_pwr_mask);
    ret |= write_reg(drv, ES7210_MIC3_POWER_REG49, mic_pwr_val);
    ret |= write_reg(drv, ES7210_MIC4_POWER_REG4A, mic_pwr_mask);
    if (ret) return ESP_FAIL;

    /* 13. PGA 增益: 按 mic_mask 使能的通道配置 */
    uint8_t pga_val = 0x10 | (cfg->pga_gain & 0x0F);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC1) write_reg(drv, ES7210_MIC1_GAIN_REG43, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC2) write_reg(drv, ES7210_MIC2_GAIN_REG44, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC3) write_reg(drv, ES7210_MIC3_GAIN_REG45, pga_val);
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC4) write_reg(drv, ES7210_MIC4_GAIN_REG46, pga_val);

    /* 14. 数字音量: 191 = 0dB (按使能通道, MICn→ADCn 顺序映射) */
    for (int i = 0; i < drv->mic_count; i++) {
        uint8_t vol_reg = ES7210_ADC1_DIRECT_DB_REG1B + drv->mic_channels[i];  /* 0x1B→ADC1, 0x1C→ADC2, ... */
        write_reg(drv, vol_reg, 191);
    }

    /* 15. DMIC freq + 其他时钟辅助寄存器 */
    write_reg(drv, ES7210_DMIC_FREQ_REG0E, 0x0A);
    write_reg(drv, ES7210_REG0D, 0x09);
    write_reg(drv, ES7210_REG0F, 0xFF);

    ESP_LOGI(TAG, "ES7210 probe 完成, mic_mask=0x%02X, pga_gain=%d",
             cfg->mic_mask, cfg->pga_gain);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * ES7210 上电 (参考 Everest_set_bias_on)
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t es7210_power_on(es7210_drv_t *drv)
{
    /* probe 阶段已完成寄存器配置, 这里开启 ADC 时钟 + 模拟上电 */
    esp_err_t ret = write_reg(drv, ES7210_CLOCK_OFF_REG01, 0x00);
    if (ret) return ESP_FAIL;


    ret  = write_reg(drv, ES7210_POWER_DOWN_REG06, 0x00);
    ret |= write_reg(drv, ES7210_ANALOG_REG40,      0x42);
    ret |= write_reg(drv, ES7210_REG0B,             0x02);
    ret |= write_reg(drv, ES7210_MIC12_POWER_REG4B, 0x0F);
    ret |= write_reg(drv, ES7210_MIC34_POWER_REG4C, 0x0F);
    if (ret) return ESP_FAIL;

    ret  = write_reg(drv, ES7210_RESET_REG00, 0x31);
    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= write_reg(drv, ES7210_RESET_REG00, 0x01);
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
    ret = es7210_probe(drv, cfg);
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

void es7210_drv_dump_regs(es7210_drv_handle_t handle)
{
    if (!handle) return;

    es7210_drv_t *drv = (es7210_drv_t *)handle;
    uint8_t val;

    ESP_LOGI(TAG, "══════════ ES7210 寄存器 DUMP ══════════");

    ESP_LOGI(TAG, "── 系统 ──");
    DUMP_REG(ES7210_RESET_REG00);
    DUMP_REG(ES7210_CLOCK_OFF_REG01);
    DUMP_REG(ES7210_CHIP_ID_REG3F);

    ESP_LOGI(TAG, "── 时钟 ──");
    DUMP_REG(ES7210_MAINCLK_REG02);
    DUMP_REG(ES7210_MASTER_CLK_REG03);
    DUMP_REG(ES7210_LRCK_DIVH_REG04);
    DUMP_REG(ES7210_LRCK_DIVL_REG05);
    DUMP_REG(ES7210_OSR_REG07);

    ESP_LOGI(TAG, "── 模式 ──");
    DUMP_REG(ES7210_MODE_CONFIG_REG08);
    DUMP_REG(ES7210_TIME_CONTROL0_REG09);
    DUMP_REG(ES7210_TIME_CONTROL1_REG0A);
    DUMP_REG(ES7210_REG0B);
    DUMP_REG(ES7210_REG0D);
    DUMP_REG(ES7210_DMIC_FREQ_REG0E);
    DUMP_REG(ES7210_REG0F);

    ESP_LOGI(TAG, "── 音频格式 ──");
    DUMP_REG(ES7210_SDP_INTERFACE1_REG11);
    DUMP_REG(ES7210_SDP_INTERFACE2_REG12);
    DUMP_REG(ES7210_ADC34_MUTERANGE_REG14);
    DUMP_REG(ES7210_ADC12_MUTE_REG15);

    ESP_LOGI(TAG, "── 模拟前端 ──");
    DUMP_REG(ES7210_ANALOG_REG40);
    DUMP_REG(ES7210_MIC12_BIAS_REG41);
    DUMP_REG(ES7210_MIC34_BIAS_REG42);

    ESP_LOGI(TAG, "── PGA 增益 ──");
    DUMP_REG(ES7210_MIC1_GAIN_REG43);
    DUMP_REG(ES7210_MIC2_GAIN_REG44);
    DUMP_REG(ES7210_MIC3_GAIN_REG45);
    DUMP_REG(ES7210_MIC4_GAIN_REG46);

    ESP_LOGI(TAG, "── MIC 电源 ──");
    DUMP_REG(ES7210_MIC1_POWER_REG47);
    DUMP_REG(ES7210_MIC2_POWER_REG48);
    DUMP_REG(ES7210_MIC3_POWER_REG49);
    DUMP_REG(ES7210_MIC4_POWER_REG4A);
    DUMP_REG(ES7210_MIC12_POWER_REG4B);
    DUMP_REG(ES7210_MIC34_POWER_REG4C);

    ESP_LOGI(TAG, "── 音量 & 信号路径 ──");
    DUMP_REG(ES7210_ADC1_DIRECT_DB_REG1B);
    DUMP_REG(ES7210_ADC2_DIRECT_DB_REG1C);
    DUMP_REG(ES7210_ADC3_DIRECT_DB_REG1D);
    DUMP_REG(ES7210_ADC4_DIRECT_DB_REG1E);
    DUMP_REG(ES7210_ADC34_HPF2_REG20);
    DUMP_REG(ES7210_ADC34_HPF1_REG21);
    DUMP_REG(ES7210_ADC12_HPF2_REG22);
    DUMP_REG(ES7210_ADC12_HPF1_REG23);

    ESP_LOGI(TAG, "── DMIC ──");
    DUMP_REG(ES7210_DMIC_CONFIG_REG10);

    ESP_LOGI(TAG, "── 电源 ──");
    DUMP_REG(ES7210_POWER_DOWN_REG06);

    ESP_LOGI(TAG, "══════════ DUMP 完成 ══════════");
}

esp_err_t es7210_drv_deinit(es7210_drv_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    es7210_drv_t *drv = (es7210_drv_t *)handle;

    /* 下电 ES7210 */
    write_reg(drv, ES7210_POWER_DOWN_REG06, 0x00);
    write_reg(drv, ES7210_MIC12_POWER_REG4B, 0xFF);
    write_reg(drv, ES7210_MIC34_POWER_REG4C, 0xFF);
    write_reg(drv, ES7210_ANALOG_REG40, 0x80);
    write_reg(drv, ES7210_CLOCK_OFF_REG01, 0x7F);
    write_reg(drv, ES7210_POWER_DOWN_REG06, 0x07);

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
