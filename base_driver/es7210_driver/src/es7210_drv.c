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
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>

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
    uint8_t                 mic_mask;
    uint8_t                 pga_gain;
    uint8_t                 mic_count;      /* 有效 MIC 通道数 */
    uint8_t                 mic_channels[4]; /* 已使能的通道号 (0-based) */
    uint8_t                 total_slots;    /* I2S 总 slot 数 */
    bool                    is_es7210l;     /* 芯片版本 */
    int                     din_io;         /* 当前 RX DIN 引脚 */
    uint8_t                 mic_slot;       /* 0=左 slot, 1=右 slot */
    bool                    ref_example;    /* 04-audio_es7210 官方例程模式 */
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

static esp_err_t update_reg_bit(es7210_drv_t *drv, uint8_t reg,
                                uint8_t mask, uint8_t val)
{
    uint8_t regv = 0;
    esp_err_t ret = read_reg(drv, reg, &regv);
    if (ret != ESP_OK) {
        return ret;
    }
    regv = (regv & (uint8_t)~mask) | (mask & val);
    return write_reg(drv, reg, regv);
}

/*
 * 板级 MIC 通路 (REG4B: bit0=MIC1偏置, bit4=ADC1, 单 MIC1 写 0x11)
 * 勿用 esp_codec_dev 的 REG4B=0x00, 本板会静音
 */
static esp_err_t es7210_mic_select(es7210_drv_t *drv, uint8_t mic_mask, uint8_t pga_gain)
{
    esp_err_t ret = ESP_OK;
    uint8_t mic12_pwr = 0;
    uint8_t mic34_pwr = 0;

    if (!(mic_mask & (ES7210_DRV_SEL_MIC1 | ES7210_DRV_SEL_MIC2 |
                      ES7210_DRV_SEL_MIC3 | ES7210_DRV_SEL_MIC4))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mic_mask & ES7210_DRV_SEL_MIC1) {
        ret |= update_reg_bit(drv, ES7210_CLOCK_OFF_REG01, 0x0B, 0x00);
        mic12_pwr |= 0x11;
        ret |= update_reg_bit(drv, ES7210_MIC1_GAIN_REG43, 0x10, 0x10);
        ret |= update_reg_bit(drv, ES7210_MIC1_GAIN_REG43, 0x0F, pga_gain & 0x0F);
    }
    if (mic_mask & ES7210_DRV_SEL_MIC2) {
        ret |= update_reg_bit(drv, ES7210_CLOCK_OFF_REG01, 0x0B, 0x00);
        mic12_pwr |= 0x22;
        ret |= update_reg_bit(drv, ES7210_MIC2_GAIN_REG44, 0x10, 0x10);
        ret |= update_reg_bit(drv, ES7210_MIC2_GAIN_REG44, 0x0F, pga_gain & 0x0F);
    }
    if (mic_mask & ES7210_DRV_SEL_MIC3) {
        ret |= update_reg_bit(drv, ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
        mic34_pwr |= 0x11;
        ret |= update_reg_bit(drv, ES7210_MIC3_GAIN_REG45, 0x10, 0x10);
        ret |= update_reg_bit(drv, ES7210_MIC3_GAIN_REG45, 0x0F, pga_gain & 0x0F);
    }
    if (mic_mask & ES7210_DRV_SEL_MIC4) {
        ret |= update_reg_bit(drv, ES7210_CLOCK_OFF_REG01, 0x15, 0x00);
        mic34_pwr |= 0x22;
        ret |= update_reg_bit(drv, ES7210_MIC4_GAIN_REG46, 0x10, 0x10);
        ret |= update_reg_bit(drv, ES7210_MIC4_GAIN_REG46, 0x0F, pga_gain & 0x0F);
    }

    if (mic12_pwr) {
        ret |= write_reg(drv, ES7210_MIC12_POWER_REG4B, mic12_pwr);
    }
    if (mic34_pwr) {
        ret |= write_reg(drv, ES7210_MIC34_POWER_REG4C, mic34_pwr);
    } else if (mic_mask & (ES7210_DRV_SEL_MIC1 | ES7210_DRV_SEL_MIC2)) {
        ret |= write_reg(drv, ES7210_MIC34_POWER_REG4C, 0x00);
    }

    if (mic_mask & ES7210_DRV_SEL_MIC1) {
        uint8_t pwr = 0;
        read_reg(drv, ES7210_MIC12_POWER_REG4B, &pwr);
        if (pwr != 0x11) {
            write_reg(drv, ES7210_MIC12_POWER_REG4B, 0x11);
            ESP_LOGW(TAG, "REG4B 校正: 0x%02X -> 0x11", pwr);
        }
    }

    int mic_num = 0;
    for (int i = 0; i < 4; i++) {
        if (mic_mask & (1 << i)) {
            mic_num++;
        }
    }
    (void)mic_num;
    /* 本板标准 I2S 立体声 (REG12=0x00), 不用 TDM */
    ret |= write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x00);
    return ret;
}

/* 本板寄存器补丁 (与 mic_adc.c 一致); REG4B=0x10 时只有嘀声无人声 */
static esp_err_t es7210_apply_board_patch(es7210_drv_t *drv)
{
    esp_err_t ret = ESP_OK;
    uint8_t pga_val = (uint8_t)(0x10 | (drv->pga_gain & 0x0F));

    ret |= write_reg(drv, ES7210_ANALOG_REG40, 0x42);
    ret |= write_reg(drv, ES7210_MIC12_BIAS_REG41, ES7210_MIC_BIAS_2V87);
    ret |= write_reg(drv, ES7210_MIC34_BIAS_REG42, ES7210_MIC_BIAS_2V87);
    ret |= write_reg(drv, ES7210_MIC1_POWER_REG47, 0x3E);
    ret |= write_reg(drv, ES7210_MIC2_POWER_REG48, 0x1E);
    ret |= write_reg(drv, ES7210_MIC3_POWER_REG49, 0x3E);
    ret |= write_reg(drv, ES7210_MIC4_POWER_REG4A, 0x1E);
    {
        uint8_t mic34_pwr = 0x00;
        if (drv->mic_mask & ES7210_DRV_SEL_MIC3) {
            mic34_pwr |= 0x11;
        }
        if (drv->mic_mask & ES7210_DRV_SEL_MIC4) {
            mic34_pwr |= 0x22;
        }
        ret |= write_reg(drv, ES7210_MIC34_POWER_REG4C, mic34_pwr);
    }
    ret |= write_reg(drv, ES7210_ADC_AUTOMUTE_REG13, 0x00);
    ret |= write_reg(drv, ES7210_ADC1_DIRECT_DB_REG1B, 0xBF);
    ret |= write_reg(drv, ES7210_ADC2_DIRECT_DB_REG1C, 0xBF);
    ret |= write_reg(drv, ES7210_ADC3_DIRECT_DB_REG1D, 0xBF);
    ret |= write_reg(drv, ES7210_ADC4_DIRECT_DB_REG1E, 0xBF);
    ret |= write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x00);

    if (drv->mic_mask & ES7210_DRV_SEL_MIC1) {
        ret |= write_reg(drv, ES7210_MIC1_GAIN_REG43, pga_val);
    }
    if (drv->mic_mask & ES7210_DRV_SEL_MIC2) {
        ret |= write_reg(drv, ES7210_MIC2_GAIN_REG44, pga_val);
    }
    if (drv->mic_mask & ES7210_DRV_SEL_MIC3) {
        ret |= write_reg(drv, ES7210_MIC3_GAIN_REG45, pga_val);
    }
    if (drv->mic_mask & ES7210_DRV_SEL_MIC4) {
        ret |= write_reg(drv, ES7210_MIC4_GAIN_REG46, pga_val);
    }

    uint8_t pwr = 0;
    for (int i = 0; i < 8; i++) {
        ret |= write_reg(drv, ES7210_MIC12_POWER_REG4B, 0x11);
        vTaskDelay(pdMS_TO_TICKS(5));
        read_reg(drv, ES7210_MIC12_POWER_REG4B, &pwr);
        if (pwr == 0x11) {
            break;
        }
        ESP_LOGW(TAG, "REG4B 重试 %d: 读回 0x%02X", i + 1, pwr);
    }
    if (pwr != 0x11) {
        ESP_LOGE(TAG, "REG4B=0x%02X (需要 0x11), MIC1 偏置未开", pwr);
        ret = ESP_FAIL;
    }
    return ret;
}

/* 仅恢复通路, 不做二次 RESET (二次 RESET 会把模拟前端打回静音) */
static void es7210_prepare_for_capture(es7210_drv_t *drv)
{
    write_reg(drv, ES7210_CLOCK_OFF_REG01, 0x00);
    write_reg(drv, ES7210_POWER_DOWN_REG06, 0x00);
    if (drv->ref_example) {
        return;
    }
    es7210_apply_board_patch(drv);
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
    const coeff_div_t *coeff_div = es7210_get_coeff(mclk_freq_hz, sample_rate_hz);
    if (!coeff_div) {
        ESP_LOGE(TAG, "无时钟系数 mclk=%"PRIu32" sr=%"PRIu32, mclk_freq_hz, sample_rate_hz);
        return ESP_FAIL;
    }

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
    write_reg(drv, ES7210_ANALOG_REG40, 0x42);

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

/* 04-audio_es7210 官方 es7210_config_codec (ANALOG=0xC3, REG4B/4C=0x0F, TDM) */
static esp_err_t es7210_config_codec_ref(es7210_drv_t *drv, uint32_t sample_rate_hz)
{
    es7210_codec_config_t codec_conf = {
        .i2s_format     = ES7210_I2S_FMT_I2S,
        .mclk_ratio     = 256,
        .sample_rate_hz = sample_rate_hz,
        .bit_width      = ES7210_I2S_BITS_16B,
        .mic_bias       = ES7210_MIC_BIAS_2V87,
        .mic_gain       = ES7210_MIC_GAIN_30DB,
        .flags.tdm_enable = true,
    };

    write_reg(drv, ES7210_RESET_REG00, 0xFF);
    write_reg(drv, ES7210_RESET_REG00, 0x32);
    write_reg(drv, ES7210_TIME_CONTROL0_REG09, 0x30);
    write_reg(drv, ES7210_TIME_CONTROL1_REG0A, 0x30);
    write_reg(drv, ES7210_ADC12_HPF1_REG23, 0x2A);
    write_reg(drv, ES7210_ADC12_HPF2_REG22, 0x0A);
    write_reg(drv, ES7210_ADC34_HPF1_REG21, 0x2A);
    write_reg(drv, ES7210_ADC34_HPF2_REG20, 0x0A);

    es7210_set_i2s_format(drv, codec_conf.i2s_format, codec_conf.bit_width, true);
    write_reg(drv, ES7210_ANALOG_REG40, 0xC3);
    es7210_set_mic_bias(drv, codec_conf.mic_bias);
    es7210_set_mic_gain(drv, codec_conf.mic_gain);
    write_reg(drv, ES7210_MIC1_POWER_REG47, 0x08);
    write_reg(drv, ES7210_MIC2_POWER_REG48, 0x08);
    write_reg(drv, ES7210_MIC3_POWER_REG49, 0x08);
    write_reg(drv, ES7210_MIC4_POWER_REG4A, 0x08);
    es7210_set_i2s_sample_rate(drv, sample_rate_hz, 256);
    write_reg(drv, ES7210_POWER_DOWN_REG06, 0x04);
    write_reg(drv, ES7210_MIC12_POWER_REG4B, 0x0F);
    write_reg(drv, ES7210_MIC34_POWER_REG4C, 0x0F);
    write_reg(drv, ES7210_RESET_REG00, 0x71);
    write_reg(drv, ES7210_RESET_REG00, 0x41);
    return ESP_OK;
}

/* 04-audio_es7210: I2S TDM 仅 RX, GPIO14 DIN, slot0|slot1 */
static esp_err_t i2s_init_ref_example(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num         = 6;
    chan_cfg.dma_frame_num        = 240;
    chan_cfg.auto_clear_after_cb  = true;
    chan_cfg.auto_clear_before_cb = false;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &drv->rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ref i2s_new_channel fail: %s", esp_err_to_name(ret));
        return ret;
    }
    drv->tx_chan = NULL;

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = cfg->sample_rate,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1),
        .gpio_cfg = {
            .mclk = cfg->mclk_io,
            .bclk = cfg->bclk_io,
            .ws   = cfg->ws_io,
            .dout = I2S_GPIO_UNUSED,
            .din  = cfg->din_io,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_tdm_mode(drv->rx_chan, &tdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ref i2s_channel_init_tdm_mode fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2s_channel_enable(drv->rx_chan);
    if (ret != ESP_OK) {
        return ret;
    }

    drv->total_slots = 2;
    drv->din_io      = cfg->din_io;
    drv->mic_slot    = 0;
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════════
 * I2S DIN 引脚探测 (GPIO10 原理图 vs GPIO14 实测)
 * ═══════════════════════════════════════════════════════════════ */
#define DIN_PROBE_FRAMES  256

static esp_err_t es7210_set_rx_din(es7210_drv_t *drv, int din_io)
{
    esp_err_t ret = i2s_channel_disable(drv->rx_chan);
    if (ret != ESP_OK) {
        return ret;
    }

    i2s_std_gpio_config_t gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = I2S_GPIO_UNUSED,
        .ws   = I2S_GPIO_UNUSED,
        .dout = I2S_GPIO_UNUSED,
        .din  = din_io,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv   = false,
        },
    };
    ret = i2s_channel_reconfig_std_gpio(drv->rx_chan, &gpio_cfg);
    if (ret != ESP_OK) {
        return ret;
    }
    drv->din_io = din_io;
    return i2s_channel_enable(drv->rx_chan);
}

typedef struct {
    int32_t min_l;
    int32_t max_l;
    int32_t min_r;
    int32_t max_r;
} stereo_stats_t;

static void stereo_stats_reset(stereo_stats_t *st)
{
    st->min_l = INT32_MAX;
    st->max_l = INT32_MIN;
    st->min_r = INT32_MAX;
    st->max_r = INT32_MIN;
}

static void stereo_stats_feed(stereo_stats_t *st, const int16_t *buf, int frames, int slots)
{
    for (int i = 0; i < frames; i++) {
        int16_t vl = buf[i * slots];
        int16_t vr = (slots > 1) ? buf[i * slots + 1] : 0;
        if (vl < st->min_l) {
            st->min_l = vl;
        }
        if (vl > st->max_l) {
            st->max_l = vl;
        }
        if (vr < st->min_r) {
            st->min_r = vr;
        }
        if (vr > st->max_r) {
            st->max_r = vr;
        }
    }
}

/* 真人声应有正负半周; 排除 GPIO14 干扰、右声道 pop、I2S 饱和尖峰 */
static int channel_score(int32_t minv, int32_t maxv)
{
    if (maxv <= minv) {
        return 0;
    }
    if (minv <= -30000 || maxv >= 30000) {
        return 0;
    }
    /* GPIO14 伪信号: 全正且幅度大 */
    if (minv >= 0 && maxv > 500) {
        return 0;
    }
    /* 上电 pop: 仅负半周, max≈0 (如 R[-127~0]) */
    if (maxv <= 8 && minv < -15) {
        return 0;
    }
    /* 仅正直流偏置 (关 HPF 时常见), 非交流声 */
    if (minv >= 0 && maxv > 30) {
        return (int)maxv / 4;
    }
    int range = (int)(maxv - minv);
    /* 真人声: 正负都有 */
    if (minv < 0 && maxv > 20) {
        return range * 2;
    }
    return range;
}

static uint8_t pick_i2s_slot(int32_t min_l, int32_t max_l, int32_t min_r, int32_t max_r)
{
    bool bip_l = (min_l < -20 && max_l > 20);
    bool bip_r = (min_r < -20 && max_r > 20);
    if (bip_l && !bip_r) {
        return 0;
    }
    if (bip_r && !bip_l) {
        return 1;
    }
    int s0 = channel_score(min_l, max_l);
    int s1 = channel_score(min_r, max_r);
    if (s1 > s0) {
        return 1;
    }
    return 0;
}

static int es7210_probe_pin(es7210_drv_t *drv, int din_io, int16_t *buf)
{
    if (es7210_set_rx_din(drv, din_io) != ESP_OK) {
        return 0;
    }
    es7210_prepare_for_capture(drv);
    vTaskDelay(pdMS_TO_TICKS(200));

    int slots = drv->total_slots ? drv->total_slots : 2;
    stereo_stats_t early;
    stereo_stats_t late;
    stereo_stats_reset(&early);
    stereo_stats_reset(&late);

    for (int pass = 0; pass < 20; pass++) {
        size_t bytes_read = 0;
        size_t bytes_req = (size_t)DIN_PROBE_FRAMES * sizeof(int16_t) * slots;
        esp_err_t ret = i2s_channel_read(drv->rx_chan, buf, bytes_req, &bytes_read, 500);
        if (ret == ESP_OK && bytes_read > 0) {
            int frames = (int)(bytes_read / (sizeof(int16_t) * slots));
            if (pass < 8) {
                stereo_stats_feed(&early, buf, frames, slots);
            } else if (pass >= 14) {
                stereo_stats_feed(&late, buf, frames, slots);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    int s0 = channel_score(late.min_l, late.max_l);
    int s1 = channel_score(late.min_r, late.max_r);
    int e1 = channel_score(early.min_r, early.max_r);

    /* 右声道常见上电 pop: 前段大、稳态段接近 0 */
    if (e1 > 80 && s1 < 40) {
        ESP_LOGI(TAG, "GPIO%d R 上电瞬态 %ld~%ld → 稳态 %ld~%ld (忽略瞬态)",
                 din_io, (long)early.min_r, (long)early.max_r,
                 (long)late.min_r, (long)late.max_r);
        s1 = 0;
    }

    int pin_score = (s0 > s1) ? s0 : s1;
    ESP_LOGI(TAG, "GPIO%d 稳态 L[%ld~%ld]分=%d R[%ld~%ld]分=%d",
             din_io, (long)late.min_l, (long)late.max_l, s0,
             (long)late.min_r, (long)late.max_r, s1);
    return pin_score;
}

static void es7210_auto_select_din(es7210_drv_t *drv, int primary, int alt, int16_t *buf)
{
    drv->mic_slot = 0;

    if (primary == alt) {
        drv->din_io = primary;
        return;
    }

    int score_pri = es7210_probe_pin(drv, primary, buf);
    int score_alt = es7210_probe_pin(drv, alt, buf);

    int best_din = primary;
    int best_score = score_pri;
    if (score_alt > score_pri) {
        best_din = alt;
        best_score = score_alt;
    }

    if (es7210_set_rx_din(drv, best_din) != ESP_OK) {
        ESP_LOGW(TAG, "DIN 切换 GPIO%d 失败, 回退 GPIO%d", best_din, primary);
        es7210_set_rx_din(drv, primary);
        best_din = primary;
    } else {
        es7210_prepare_for_capture(drv);
    }

    stereo_stats_t st;
    stereo_stats_reset(&st);
    int slots = drv->total_slots ? drv->total_slots : 2;
    for (int pass = 0; pass < 10; pass++) {
        size_t bytes_read = 0;
        size_t bytes_req = (size_t)DIN_PROBE_FRAMES * sizeof(int16_t) * slots;
        if (i2s_channel_read(drv->rx_chan, buf, bytes_req, &bytes_read, 500) == ESP_OK
            && bytes_read > 0) {
            int frames = (int)(bytes_read / (sizeof(int16_t) * slots));
            stereo_stats_feed(&st, buf, frames, slots);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    drv->mic_slot = pick_i2s_slot(st.min_l, st.max_l, st.min_r, st.max_r);

    ESP_LOGI(TAG, "选用 GPIO%d (稳态得分=%d), I2S slot%d, L[%ld~%ld] R[%ld~%ld]",
             best_din, best_score, drv->mic_slot,
             (long)st.min_l, (long)st.max_l, (long)st.min_r, (long)st.max_r);
    if (best_score < 30) {
        ESP_LOGW(TAG, "稳态信号极弱, MIC 模拟前端可能未工作");
    }
}

/* ═══════════════════════════════════════════════════════════════
 * I2S 初始化
 * ═══════════════════════════════════════════════════════════════ */
static esp_err_t i2s_init(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    i2s_chan_config_t chan_cfg = {
        .id                  = I2S_NUM_0,
        .role                = I2S_ROLE_MASTER,
        .dma_desc_num        = 6,
        .dma_frame_num       = 240,
        .auto_clear_after_cb  = true,
        .auto_clear_before_cb = false,
        .intr_priority       = 0,
    };

    int slots = cfg->total_slots;
    if (slots == 0) {
        slots = 2;
    }

    esp_err_t ret;

    if (slots <= 2) {
        /* 本板需 TX 输出时钟 + RX 收数据; 纯 RX 主模式会 OUT≈0 */
        ret = i2s_new_channel(&chan_cfg, &drv->tx_chan, &drv->rx_chan);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_new_channel fail: %s", esp_err_to_name(ret));
            return ret;
        }

        i2s_std_config_t tx_cfg = {
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
                .left_align     = true,
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
        ret = i2s_channel_init_std_mode(drv->tx_chan, &tx_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_std_mode(TX) fail: %s", esp_err_to_name(ret));
            return ret;
        }

        i2s_std_config_t rx_cfg = {
            .clk_cfg = tx_cfg.clk_cfg,
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
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_GPIO_UNUSED,
                .ws   = I2S_GPIO_UNUSED,
                .dout = I2S_GPIO_UNUSED,
                .din  = cfg->din_io,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv   = false,
                },
            },
        };

        ret = i2s_channel_init_std_mode(drv->rx_chan, &rx_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_init_std_mode(RX) fail: %s", esp_err_to_name(ret));
            return ret;
        }

        ret = i2s_channel_enable(drv->tx_chan);
        if (ret != ESP_OK) {
            return ret;
        }
        return i2s_channel_enable(drv->rx_chan);
    }

    /* TDM 多通道: 保留 TX+RX 双通道 */
    ret = i2s_new_channel(&chan_cfg, &drv->tx_chan, &drv->rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel fail: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_tdm_slot_mask_t mask = 0;
    for (int i = 0; i < slots; i++) {
        mask |= (1 << i);
    }

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

    ret = i2s_channel_enable(drv->tx_chan);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2s_channel_enable(drv->rx_chan);
}

static esp_err_t es7210_reset(es7210_drv_t *drv)
{
    esp_err_t ret;
    ret  = write_reg(drv, ES7210_RESET_REG00, 0xFF);
    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= write_reg(drv, ES7210_RESET_REG00, 0x00);
    return ret;
}

static esp_err_t es7210_probe(es7210_drv_t *drv, const es7210_drv_config_t *cfg)
{
    esp_err_t ret;

    ret = es7210_reset(drv);
    if (ret) {
        return ESP_FAIL;
    }

    ret  = write_reg(drv, ES7210_MODE_CONFIG_REG08, 0x00);
    ret |= write_reg(drv, ES7210_MASTER_CLK_REG03, 0x04);
    ret |= es7210_set_i2s_sample_rate(drv, cfg->sample_rate, 256);
    if (ret) {
        return ESP_FAIL;
    }

    ret  = write_reg(drv, ES7210_TIME_CONTROL0_REG09, 0x30);
    ret |= write_reg(drv, ES7210_TIME_CONTROL1_REG0A, 0x30);
    ret |= write_reg(drv, ES7210_ADC34_HPF2_REG20, 0x0A);
    ret |= write_reg(drv, ES7210_ADC34_HPF1_REG21, 0x2A);
    ret |= write_reg(drv, ES7210_ADC12_HPF2_REG22, 0x0A);
    ret |= write_reg(drv, ES7210_ADC12_HPF1_REG23, 0x2A);
    if (ret) {
        return ESP_FAIL;
    }

    ret  = write_reg(drv, ES7210_SDP_INTERFACE1_REG11, 0x60);
    ret |= write_reg(drv, ES7210_SDP_INTERFACE2_REG12, 0x00);
    ret |= write_reg(drv, ES7210_ADC34_MUTERANGE_REG14, 0x00);
    ret |= write_reg(drv, ES7210_ADC12_MUTE_REG15, 0x00);
    ret |= write_reg(drv, ES7210_ADC_AUTOMUTE_REG13, 0x00);
    ret |= write_reg(drv, ES7210_DMIC_CONFIG_REG10, 0x00);
    if (ret) {
        return ESP_FAIL;
    }

    /* 本板: 片内偏置 2.87V (勿用 bias=0x00 外部 LDO 流程) */
    ret  = write_reg(drv, ES7210_ANALOG_REG40, 0x42);
    ret |= write_reg(drv, ES7210_MIC12_BIAS_REG41, ES7210_MIC_BIAS_2V87);
    ret |= write_reg(drv, ES7210_MIC34_BIAS_REG42, ES7210_MIC_BIAS_2V87);
    if (ret) {
        return ESP_FAIL;
    }

    uint8_t chip_id;
    ret = read_reg(drv, ES7210_CHIP_ID_REG3F, &chip_id);
    if (ret) {
        return ESP_FAIL;
    }
    drv->is_es7210l = ((chip_id >> 4) != 0x00);
    ESP_LOGI(TAG, "检测到 %s (CHIP_ID=0x%02X)",
             drv->is_es7210l ? "ES7210L" : "ES7210", chip_id);

    /* 本板 MIC 电源: 0x3E/0x1E (勿写 esp_codec_dev 的 0x08) */
    ret  = write_reg(drv, ES7210_MIC1_POWER_REG47, 0x3E);
    ret |= write_reg(drv, ES7210_MIC2_POWER_REG48, 0x1E);
    ret |= write_reg(drv, ES7210_MIC3_POWER_REG49, 0x3E);
    ret |= write_reg(drv, ES7210_MIC4_POWER_REG4A, 0x1E);
    if (ret) {
        return ESP_FAIL;
    }

    uint8_t pga_val = (uint8_t)(0x10 | (cfg->pga_gain & 0x0F));
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC1) {
        write_reg(drv, ES7210_MIC1_GAIN_REG43, pga_val);
    }
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC2) {
        write_reg(drv, ES7210_MIC2_GAIN_REG44, pga_val);
    }
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC3) {
        write_reg(drv, ES7210_MIC3_GAIN_REG45, pga_val);
    }
    if (cfg->mic_mask & ES7210_DRV_SEL_MIC4) {
        write_reg(drv, ES7210_MIC4_GAIN_REG46, pga_val);
    }

    for (int i = 0; i < drv->mic_count; i++) {
        uint8_t vol_reg = (uint8_t)(ES7210_ADC1_DIRECT_DB_REG1B + drv->mic_channels[i]);
        write_reg(drv, vol_reg, 191); /* 0dB, 与参考工程 EXAMPLE_ES7210_ADC_VOLUME=0 一致 */
    }

    write_reg(drv, ES7210_DMIC_FREQ_REG0E, 0x0A);
    write_reg(drv, ES7210_REG0D, 0x09);
    write_reg(drv, ES7210_REG0F, 0xFF);

    ESP_LOGI(TAG, "ES7210 probe 完成, mic_mask=0x%02X, pga_gain=%d",
             cfg->mic_mask, cfg->pga_gain);
    return ESP_OK;
}

static esp_err_t es7210_power_on(es7210_drv_t *drv)
{
    esp_err_t ret;

    ret  = write_reg(drv, ES7210_CLOCK_OFF_REG01, 0x00);
    ret |= write_reg(drv, ES7210_POWER_DOWN_REG06, 0x00);
    ret |= write_reg(drv, ES7210_ANALOG_REG40, 0x42);
    ret |= write_reg(drv, ES7210_REG0B, 0x02);
    if (ret) {
        return ESP_FAIL;
    }

    /* esp_codec_dev 官方上电 (用户曾在此配置下听到人声) */
    ret  = write_reg(drv, ES7210_MIC1_POWER_REG47, 0x08);
    ret |= write_reg(drv, ES7210_MIC2_POWER_REG48, 0x08);
    ret |= write_reg(drv, ES7210_MIC3_POWER_REG49, 0x08);
    ret |= write_reg(drv, ES7210_MIC4_POWER_REG4A, 0x08);
    if (ret) {
        return ESP_FAIL;
    }

    ret = es7210_mic_select(drv, drv->mic_mask, drv->pga_gain);
    if (ret) {
        return ESP_FAIL;
    }

    ret  = write_reg(drv, ES7210_RESET_REG00, 0x71);
    vTaskDelay(pdMS_TO_TICKS(10));
    ret |= write_reg(drv, ES7210_RESET_REG00, 0x41);
    if (ret) {
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    ret = es7210_apply_board_patch(drv);
    if (ret) {
        return ESP_FAIL;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
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

    drv->sample_rate   = cfg->sample_rate;
    drv->mic_mask      = cfg->mic_mask;
    drv->pga_gain      = cfg->pga_gain;
    drv->total_slots   = cfg->total_slots ? cfg->total_slots : 2;
    drv->mic_count     = parse_mic_channels(cfg->mic_mask, drv->mic_channels);
    drv->din_io        = cfg->din_io;
    drv->ref_example   = cfg->use_ref_example;

    esp_err_t ret;

    /* ── 1. I2C 设备挂载 ─────────────────────────────────────── */
    ret = i2cDriver_addDevice(cfg->i2c_bus, 0x40, 100000, &drv->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C add device fail");
        goto fail_i2c;
    }

    if (cfg->use_ref_example) {
        ret = i2s_init_ref_example(drv, cfg);
        if (ret != ESP_OK) {
            goto fail_i2s;
        }
        ret = es7210_config_codec_ref(drv, cfg->sample_rate);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ES7210 ref codec config fail");
            goto fail_probe;
        }
        es7210_config_volume(drv, 0);
        ESP_LOGI(TAG, "参考例程模式: SR=%"PRIu32"Hz, TDM, DIN=GPIO%d",
                 cfg->sample_rate, cfg->din_io);
        *handle = drv;
        return ESP_OK;
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

    int16_t *din_buf = heap_caps_malloc(DIN_PROBE_FRAMES * 2 * sizeof(int16_t),
                                        MALLOC_CAP_INTERNAL);
    if (din_buf) {
        es7210_auto_select_din(drv, cfg->din_io, ES7210_I2S_DIN_PIN, din_buf);
        free(din_buf);
    } else {
        ESP_LOGW(TAG, "DIN 探测缓冲分配失败, 使用 GPIO%d", cfg->din_io);
    }

    ESP_LOGI(TAG, "初始化完成: SR=%"PRIu32"Hz, mics=%d, DIN=GPIO%d, slot=%d",
             cfg->sample_rate, drv->mic_count, drv->din_io, drv->mic_slot);

    *handle = drv;
    return ESP_OK;

fail_probe:
    if (drv->rx_chan) {
        i2s_channel_disable(drv->rx_chan);
        i2s_del_channel(drv->rx_chan);
    }
    if (drv->tx_chan) {
        i2s_channel_disable(drv->tx_chan);
        i2s_del_channel(drv->tx_chan);
    }
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

    esp_err_t ret = i2s_channel_read(drv->rx_chan, buf, bytes_req, &bytes_read, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(ret));
        return -1;
    }
    return (int)(bytes_read / (sizeof(int16_t) * slots));
}

uint8_t es7210_drv_get_slots(es7210_drv_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    es7210_drv_t *drv = (es7210_drv_t *)handle;
    return drv->total_slots ? drv->total_slots : 2;
}

void es7210_drv_prepare_capture(es7210_drv_handle_t handle)
{
    if (!handle) {
        return;
    }
    es7210_prepare_for_capture((es7210_drv_t *)handle);
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

int es7210_drv_get_din_pin(es7210_drv_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    return ((es7210_drv_t *)handle)->din_io;
}

uint8_t es7210_drv_get_mic_slot(es7210_drv_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return ((es7210_drv_t *)handle)->mic_slot;
}

uint8_t es7210_drv_get_mic_mask(es7210_drv_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    return ((es7210_drv_t *)handle)->mic_mask;
}

static const char *mic_mask_name(uint8_t mask)
{
    switch (mask) {
    case ES7210_DRV_SEL_MIC1: return "MIC1";
    case ES7210_DRV_SEL_MIC2: return "MIC2";
    case ES7210_DRV_SEL_MIC3: return "MIC3";
    case ES7210_DRV_SEL_MIC4: return "MIC4";
    default:                  return "MIC?";
    }
}

esp_err_t es7210_drv_switch_mic(es7210_drv_handle_t handle, uint8_t mic_mask)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!(mic_mask & (ES7210_DRV_SEL_MIC1 | ES7210_DRV_SEL_MIC2 |
                      ES7210_DRV_SEL_MIC3 | ES7210_DRV_SEL_MIC4))) {
        return ESP_ERR_INVALID_ARG;
    }

    es7210_drv_t *drv = (es7210_drv_t *)handle;
    drv->mic_mask  = mic_mask;
    drv->mic_count = (uint8_t)parse_mic_channels(mic_mask, drv->mic_channels);
    es7210_prepare_for_capture(drv);
    esp_err_t ret = es7210_mic_select(drv, mic_mask, drv->pga_gain);
    vTaskDelay(pdMS_TO_TICKS(300));
    return ret;
}

static void measure_stereo_pcm(es7210_drv_t *drv, int16_t *buf, int frames,
                               int32_t *min_l, int32_t *max_l,
                               int32_t *min_r, int32_t *max_r)
{
    int slots = drv->total_slots ? drv->total_slots : 2;

    *min_l = INT32_MAX;
    *max_l = INT32_MIN;
    *min_r = INT32_MAX;
    *max_r = INT32_MIN;

    for (int pass = 0; pass < 24; pass++) {
        size_t bytes_read = 0;
        size_t bytes_req  = (size_t)frames * sizeof(int16_t) * slots;
        esp_err_t ret = i2s_channel_read(drv->rx_chan, buf, bytes_req, &bytes_read, 500);
        if (ret != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (pass < 10) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        int n = (int)(bytes_read / (sizeof(int16_t) * slots));
        for (int i = 0; i < n; i++) {
            int16_t vl = buf[i * slots];
            int16_t vr = (slots > 1) ? buf[i * slots + 1] : 0;
            int32_t al = vl >= 0 ? vl : -vl;
            int32_t ar = vr >= 0 ? vr : -vr;
            if (al > 12000 || ar > 12000) {
                continue;
            }
            if (vl < *min_l) {
                *min_l = vl;
            }
            if (vl > *max_l) {
                *max_l = vl;
            }
            if (vr < *min_r) {
                *min_r = vr;
            }
            if (vr > *max_r) {
                *max_r = vr;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void fill_scan_result(es7210_mic_scan_result_t *r, uint8_t mic_mask, uint8_t slot,
                             int32_t minv, int32_t maxv)
{
    r->mic_mask  = mic_mask;
    r->i2s_slot  = slot;
    r->min_v     = minv;
    r->max_v     = maxv;
    int32_t pa = minv >= 0 ? minv : -minv;
    int32_t pb = maxv >= 0 ? maxv : -maxv;
    r->peak      = (pa > pb) ? pa : pb;
    r->score     = channel_score(minv, maxv);
}

esp_err_t es7210_drv_scan_mics(es7210_drv_handle_t handle, int16_t *buf, int buf_frames,
                               es7210_mic_scan_result_t *best)
{
    if (!handle || !buf || buf_frames <= 0 || !best) {
        return ESP_ERR_INVALID_ARG;
    }

    static const uint8_t mic_list[] = {
        ES7210_DRV_SEL_MIC1,
        ES7210_DRV_SEL_MIC2,
        ES7210_DRV_SEL_MIC3,
    };

    es7210_drv_t *drv = (es7210_drv_t *)handle;
    es7210_mic_scan_result_t winner = { 0 };
    winner.score = -1;

    ESP_LOGI(TAG, "══════ MIC1/2/3 扫描 (每路约 1.5s, 请对着麦克风说话) ══════");

    for (size_t i = 0; i < sizeof(mic_list); i++) {
        uint8_t mask = mic_list[i];
        esp_err_t err = es7210_drv_switch_mic(handle, mask);
        if (err != ESP_OK) {
            continue;
        }

        int32_t min_l, max_l, min_r, max_r;
        measure_stereo_pcm(drv, buf, buf_frames, &min_l, &max_l, &min_r, &max_r);

        es7210_mic_scan_result_t r0, r1;
        fill_scan_result(&r0, mask, 0, min_l, max_l);
        fill_scan_result(&r1, mask, 1, min_r, max_r);

        ESP_LOGI(TAG, "  %s slot0(L)[%ld~%ld] 峰=%ld 分=%d | slot1(R)[%ld~%ld] 峰=%ld 分=%d",
                 mic_mask_name(mask),
                 (long)r0.min_v, (long)r0.max_v, (long)r0.peak, r0.score,
                 (long)r1.min_v, (long)r1.max_v, (long)r1.peak, r1.score);

        if (r0.score > winner.score) {
            winner = r0;
        }
        if (r1.score > winner.score) {
            winner = r1;
        }
    }

    if (winner.score < 0) {
        winner.mic_mask = ES7210_DRV_SEL_MIC1;
        winner.i2s_slot = 0;
        winner.score    = 0;
    }

    es7210_drv_switch_mic(handle, winner.mic_mask);
    vTaskDelay(pdMS_TO_TICKS(300));

    /* 在选定 MIC 上复核 L/R: 扫描阶段 R 易被 pop 误选 */
    int32_t min_l, max_l, min_r, max_r;
    measure_stereo_pcm(drv, buf, buf_frames, &min_l, &max_l, &min_r, &max_r);
    uint8_t slot = pick_i2s_slot(min_l, max_l, min_r, max_r);
    fill_scan_result(&winner, winner.mic_mask, slot,
                     (slot == 0) ? min_l : min_r,
                     (slot == 0) ? max_l : max_r);
    drv->mic_slot = slot;
    *best = winner;

    ESP_LOGI(TAG, "扫描选用 %s I2S slot%d (L=0/R=1), 范围[%ld~%ld] 峰值=%ld 得分=%d",
             mic_mask_name(winner.mic_mask), winner.i2s_slot,
             (long)winner.min_v, (long)winner.max_v, (long)winner.peak, winner.score);
    ESP_LOGI(TAG, "  复核 L[%ld~%ld] R[%ld~%ld]",
             (long)min_l, (long)max_l, (long)min_r, (long)max_r);

    if (winner.peak < 200) {
        ESP_LOGW(TAG, "三路 MIC 峰值均 < 200, 模拟前端或焊接可能异常");
    }

    return ESP_OK;
}

uint8_t es7210_drv_read_adc1_level(es7210_drv_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    es7210_drv_t *drv = (es7210_drv_t *)handle;
    uint8_t val = 0;
    read_reg(drv, ES7210_ADC1_DIRECT_DB_REG1B, &val);
    return val;
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
