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

static const char *TAG = "es7210";
i2c_master_dev_handle_t es7210_handle = NULL;
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


typedef struct {
    uint32_t mclk;            /* mclk 频率 */
    uint32_t lrck;            /* lrck */
    uint8_t  ss_ds;           /* 未使用 */
    uint8_t  adc_div;         /* adc 时钟分频器 */
    uint8_t  dll;             /* dll 旁路 */
    uint8_t  doubler;         /* 倍频器启用 */
    uint8_t  osr;             /* adc 过采样率 */
    uint8_t  mclk_src;        /* 选择 mclk 源 */
    uint32_t lrck_h;          /* lrck 的高 4 位 */
    uint32_t lrck_l;          /* lrck 的低 8 位 */
} coeff_div_t;

static const coeff_div_t es7210_coeff_div[] = {/* 定义时钟树:需根据芯片修改 */
    /* 8k */
    {12288000,  8000,  0x00,  0x03,  0x01,  0x00,  0x20,  0x00,    0x06,  0x00},
    {16384000,  8000,  0x00,  0x04,  0x01,  0x00,  0x20,  0x00,    0x08,  0x00},
    {19200000,  8000,  0x00,  0x1e,  0x00,  0x01,  0x28,  0x00,    0x09,  0x60},
    {4096000,   8000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},

    /* 11.025k */
    {11289600,  11025,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x01,  0x00},

    /* 12k */
    {12288000,  12000,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x04,  0x00},
    {19200000,  12000,  0x00,  0x14,  0x00,  0x01,  0x28,  0x00,    0x06,  0x40},

    /* 16k */
    {4096000,   16000,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},
    {19200000,  16000,  0x00,  0x0a,  0x00,  0x00,  0x1e,  0x00,    0x04,  0x80},
    {16384000,  16000,  0x00,  0x02,  0x01,  0x00,  0x20,  0x00,    0x04,  0x00},
    {12288000,  16000,  0x00,  0x03,  0x01,  0x01,  0x20,  0x00,    0x03,  0x00},

    /* 22.05k */
    {11289600,  22050,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},

    /* 24k */
    {12288000,  24000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},
    {19200000,  24000,  0x00,  0x0a,  0x00,  0x01,  0x28,  0x00,    0x03,  0x20},

    /* 32k */
    {12288000,  32000,  0x00,  0x03,  0x00,  0x00,  0x20,  0x00,    0x01,  0x80},
    {16384000,  32000,  0x00,  0x01,  0x01,  0x00,  0x20,  0x00,    0x02,  0x00},
    {19200000,  32000,  0x00,  0x05,  0x00,  0x00,  0x1e,  0x00,    0x02,  0x58},

    /* 44.1k */
    {11289600,  44100,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},

    /* 48k */
    {12288000,  48000,  0x00,  0x01,  0x01,  0x01,  0x20,  0x00,    0x01,  0x00},
    {19200000,  48000,  0x00,  0x05,  0x00,  0x01,  0x28,  0x00,    0x01,  0x90},

    /* 64k */
    {16384000,  64000,  0x01,  0x01,  0x01,  0x00,  0x20,  0x00,    0x01,  0x00},
    {19200000,  64000,  0x00,  0x05,  0x00,  0x01,  0x1e,  0x00,    0x01,  0x2c},

    /* 88.2k */
    {11289600,  88200,  0x01,  0x01,  0x01,  0x01,  0x20,  0x00,    0x00,  0x80},

    /* 96k */
    {12288000,  96000,  0x01,  0x01,  0x01,  0x01,  0x20,  0x00,    0x00,  0x80},
    {19200000,  96000,  0x01,  0x05,  0x00,  0x01,  0x28,  0x00,    0x00,  0xc8},
};

/* 无需修改，时钟表查表函数，仅在c文件中声明，防止重复定义 */
static const coeff_div_t *es7210_get_coeff(uint32_t mclk, uint32_t lrck)
{
    for (int i = 0; i < sizeof(es7210_coeff_div) / sizeof(coeff_div_t); i++) 
    {
        if (es7210_coeff_div[i].lrck == lrck && es7210_coeff_div[i].mclk == mclk) 
        {
            return &es7210_coeff_div[i];
        }
    }
    return NULL;
}


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

/**
 * @brief       配置ES7210的I2S接口格式
 * @param       i2s_format: I2S数据格式（I2S、LJ、DSP-A、DSP-B）
 * @param       bit_width: I2S数据位宽（16/18/20/24/32位）
 * @param       tdm_enable: 是否使能TDM模式
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_i2s_format(es7210_drv_t *drv,es7210_i2s_fmt_t i2s_format, es7210_i2s_bits_t bit_width, bool tdm_enable)
{
    uint8_t reg_val = 0;

    /* 根据bit_width选择寄存器值，设置I2S数据位宽 */
    switch (bit_width) 
    {
    case ES7210_I2S_BITS_16B:
        reg_val = 0x60;
        break;
    case ES7210_I2S_BITS_18B:
        reg_val = 0x40;
        break;
    case ES7210_I2S_BITS_20B:
        reg_val = 0x20;
        break;
    case ES7210_I2S_BITS_24B:
        reg_val = 0x00;
        break;
    case ES7210_I2S_BITS_32B:
        reg_val = 0x80;
        break;
    default:
        abort(); /* 非法参数，终止程序 */
    }

    /* 配置I2S接口1寄存器，设置数据格式和位宽 */
    write_reg(drv, ES7210_SDP_INTERFACE1_REG11, i2s_format | reg_val);

    const char *mode_str = NULL;

    /* 根据i2s_format选择寄存器值，设置I2S工作模式 */
    switch (i2s_format) 
    {
    case ES7210_I2S_FMT_I2S:
        reg_val = 0x02;
        mode_str = "standard i2s";
        break;
    case ES7210_I2S_FMT_LJ:
        reg_val = 0x02;
        mode_str = "left justify";
        break;
    case ES7210_I2S_FMT_DSP_A:
        reg_val = 0x01;
        mode_str = "DSP-A";
        break;
    case ES7210_I2S_FMT_DSP_B:
        reg_val = 0x01;
        mode_str = "DSP-B";
        break;
    default:
        abort(); 
    }

    /* 配置I2S接口2寄存器，设置TDM模式或普通模式 */
    if (tdm_enable) 
    {
        write_reg(drv, ES7210_SDP_INTERFACE2_REG12, reg_val); /* 使能TDM，写入对应模式值 */
    }
    else 
    {
        write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x00); /* 普通模式，写0 */
    }

    /* 打印当前配置到日志，便于调试 */
    ESP_LOGI(TAG, "format: %s, bit width: %d, tdm mode %s", mode_str, bit_width, tdm_enable ? "enabled" : "disabled");
    return ESP_OK;
}


/**
 * @brief       设置ES7210的I2S采样率相关寄存器
 * @param       sample_rate_hz: 采样率（Hz）
 * @param       mclk_ratio: MCLK与采样率的倍频系数
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_i2s_sample_rate(es7210_drv_t *drv, uint32_t sample_rate_hz, uint32_t mclk_ratio)
{
    uint32_t mclk_freq_hz = sample_rate_hz * mclk_ratio;                            /* 计算MCLK频率 */
    const coeff_div_t *coeff_div = es7210_get_coeff(mclk_freq_hz, sample_rate_hz);  /* 查表获取分频参数 */

    /* 设置ADC过采样率（osr） */
    write_reg(drv, ES7210_OSR_REG07, coeff_div->osr);

    /* 设置ADC分频器、倍频器和DLL旁路 */
    write_reg(drv, ES7210_MAINCLK_REG02, (coeff_div->adc_div) | (coeff_div->doubler << 6) | (coeff_div->dll << 7));

    /* 设置LRCK分频高8位和低8位 */
    write_reg(drv, ES7210_LRCK_DIVH_REG04, coeff_div->lrck_h);
    write_reg(drv, ES7210_LRCK_DIVL_REG05, coeff_div->lrck_l);

    /* 打印采样率和MCLK频率，便于调试 */
    ESP_LOGI(TAG, "sample rate: %"PRIu32"Hz, mclk frequency: %"PRIu32"Hz", sample_rate_hz, mclk_freq_hz);

    return ESP_OK;
}

/**
 * @brief       设置ES7210的麦克风增益
 * @param       mic_gain: 麦克风增益枚举值
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_mic_gain(es7210_drv_t *drv, es7210_mic_gain_t mic_gain)
{
    /* 设置MIC1~MIC4的增益，|0x10为固定配置 */
    write_reg(drv, ES7210_MIC1_GAIN_REG43, mic_gain | 0x10);
    write_reg(drv, ES7210_MIC2_GAIN_REG44, mic_gain | 0x10);
    write_reg(drv, ES7210_MIC3_GAIN_REG45, mic_gain | 0x10);
    write_reg(drv, ES7210_MIC4_GAIN_REG46, mic_gain | 0x10);

    return ESP_OK;
}

/**
 * @brief       设置ES7210的麦克风偏置电压
 * @param       mic_bias: 麦克风偏置电压枚举值
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_set_mic_bias(es7210_drv_t *drv, es7210_mic_bias_t mic_bias)
{
    /* 设置MIC1/2和MIC3/4的偏置电压 */
    write_reg(drv, ES7210_MIC12_BIAS_REG41, mic_bias);
    write_reg(drv, ES7210_MIC34_BIAS_REG42, mic_bias);

    return ESP_OK;
}


/* 前向声明 */
static esp_err_t es7210_config_codec(es7210_drv_t *drv, const es7210_codec_config_t *codec_conf);
static esp_err_t es7210_config_volume(es7210_drv_t *drv, int8_t volume_db);

/**
 * @brief       ES7210初始化
 * @param       is_tdm : 是否启用TDM模式
 * @retval      ESP_OK 成功
 */
esp_err_t es7210_init(void *bus, bool is_tdm)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret;

    /* ── 1. 初始化 es7210_driver (ES7210 + I2S) ────────────────── */

    es7210_drv_config_t es_cfg = {
        .i2c_bus     = bus,
        .mic_mask    = ES7210_DRV_SEL_MIC1,
        .sample_rate = 16000,
        .pga_gain    = 6,      /* 18dB = 6 × 3dB */
        .total_slots = 2,      /* 标准 I2S 立体声 */
        .mclk_io     = ES7210_I2S_MCLK_PIN,
        .bclk_io     = ES7210_I2S_BCLK_PIN,
        .ws_io       = ES7210_I2S_LRCK_PIN,
        .din_io      = ES7210_I2S_DIN_PIN,
    };

    es7210_drv_t *drv = calloc(1, sizeof(*drv));
    if (!drv) return ESP_ERR_NO_MEM;

    drv->sample_rate = es_cfg.sample_rate;
    drv->total_slots = es_cfg.total_slots ? es_cfg.total_slots : 2;
    drv->mic_count   = parse_mic_channels(es_cfg.mic_mask, drv->mic_channels);

    /* ── 1. I2C 设备挂载 ─────────────────────────────────────── */
    ret = i2cDriver_addDevice(bus, 0x40, 100000, &es7210_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device fail");
        return ret;
    }

    es7210_codec_config_t codec_conf = {
        .i2s_format = ES7210_I2S_FMT_I2S,
        .mclk_ratio = 256,
        .sample_rate_hz = 48000,
        .bit_width = ES7210_I2S_BITS_16B,
        .mic_bias = ES7210_MIC_BIAS_2V87,     // TODO BIAS V ? 
        .mic_gain = ES7210_MIC_GAIN_37_5DB,
        .flags.tdm_enable = is_tdm
    };

    es7210_config_codec(drv, &codec_conf);
    es7210_config_volume(drv, 32);
    return ESP_OK;
}

/**
 * @brief       配置ES7210的音量（增益）
 * @param       volume_db: 音量（dB）
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_config_volume(es7210_drv_t *drv,int8_t volume_db)
{
    uint8_t reg_val = 191 + volume_db * 2; /* 计算寄存器对应的音量值 */

    /* 设置ADC1~ADC4的音量 */
    write_reg(drv, ES7210_ADC1_DIRECT_DB_REG1B, reg_val);
    write_reg(drv, ES7210_ADC2_DIRECT_DB_REG1C, reg_val);
    write_reg(drv, ES7210_ADC3_DIRECT_DB_REG1D, reg_val);
    write_reg(drv, ES7210_ADC4_DIRECT_DB_REG1E, reg_val);

    return ESP_OK;
}


/**
 * @brief       配置ES7210编解码器的主要参数
 * @param       codec_conf: 编解码器配置结构体指针
 * @retval      ESP_OK: 配置成功
 */
static esp_err_t es7210_config_codec(es7210_drv_t *drv ,const es7210_codec_config_t *codec_conf)
{
    /* 软件复位ES7210 */
    write_reg(drv, ES7210_RESET_REG00, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(10)); /* 等待复位完成 */
    write_reg(drv, ES7210_RESET_REG00, 0x32);

    /* 设置上电初始化时间 */
    write_reg(drv, ES7210_TIME_CONTROL0_REG09, 0x30);
    write_reg(drv, ES7210_TIME_CONTROL1_REG0A, 0x30);

    /* 配置ADC1-4的高通滤波器（HPF） */
    write_reg(drv, ES7210_ADC12_HPF1_REG23, 0x2A);
    write_reg(drv, ES7210_ADC12_HPF2_REG22, 0x0A);
    write_reg(drv, ES7210_ADC34_HPF1_REG21, 0x2A);
    write_reg(drv, ES7210_ADC34_HPF2_REG20, 0x0A);

    /* 设置采样位宽、I2S协议、TDM使能等 */
    es7210_set_i2s_format(drv, codec_conf->i2s_format, codec_conf->bit_width, codec_conf->flags.tdm_enable);

    /* 配置模拟电源和VMID电压 */
    write_reg(drv, ES7210_ANALOG_REG40, 0xC3);

    /* 设置MIC1-4偏置电压 */
    es7210_set_mic_bias(drv,codec_conf->mic_bias);

    /* 设置MIC1-4增益 */
    es7210_set_mic_gain(drv, codec_conf->mic_gain);

    /* 打开MIC1-4电源 */
    write_reg(drv, ES7210_MIC1_POWER_REG47, 0x08);
    write_reg(drv, ES7210_MIC2_POWER_REG48, 0x08);
    write_reg(drv, ES7210_MIC3_POWER_REG49, 0x08);
    write_reg(drv, ES7210_MIC4_POWER_REG4A, 0x08);

    /* 设置ADC采样率 */
    es7210_set_i2s_sample_rate(drv, codec_conf->sample_rate_hz, codec_conf->mclk_ratio);

    /* 关闭DLL */
    write_reg(drv, ES7210_POWER_DOWN_REG06, 0x04);

    /* 打开MIC1-4偏置、ADC1-4、PGA1-4电源 */
    write_reg(drv, ES7210_MIC12_POWER_REG4B, 0x0F);
    write_reg(drv, ES7210_MIC34_POWER_REG4C, 0x0F);

    /* 使能设备 */
    write_reg(drv, ES7210_RESET_REG00, 0x71);
    write_reg(drv, ES7210_RESET_REG00, 0x41);

    return ESP_OK;
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
    /* Mode: Slave, standard I2S, no EQ, BCLK no-invert */
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

    /* 3. HPF: fast settling + auto offset (datasheet defaults, 低噪优化) */
    /*  HPF1: OFFSET auto-update=0, COEF1=6(fast)   HPF2: COEF2=6(fast) */
    ret  = write_reg(drv, ES7210_ADC34_HPF2_REG20, 0x06);
    ret |= write_reg(drv, ES7210_ADC34_HPF1_REG21, 0x06);
    ret |= write_reg(drv, ES7210_ADC12_HPF2_REG22, 0x06);
    ret |= write_reg(drv, ES7210_ADC12_HPF1_REG23, 0x06);
    if (ret) return ESP_FAIL;


    /* 6. Audio format: S16_LE, I2S standard */
    ret = write_reg(drv, ES7210_SDP_INTERFACE1_REG11, 0x60);
    if (ret) return ESP_FAIL;

    /* 7. SDOUT: 标准 I2S, ADC12→SDOUT1 */
    ret = write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x00);
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
