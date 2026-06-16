#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ES7210 MIC 通道选择 (与 esp_codec_dev 兼容) ─────────────── */
#define ES7210_DRV_SEL_MIC1   (1 << 0)
#define ES7210_DRV_SEL_MIC2   (1 << 1)
#define ES7210_DRV_SEL_MIC3   (1 << 2)
#define ES7210_DRV_SEL_MIC4   (1 << 3)

typedef void *es7210_drv_handle_t;

/* 常用寄存器声明 */
#define  ES7210_RESET_REG00                 0x00        /* 复位控制 
                                                          bit4 ADC12 bit5 ADC34
                                                          写1 Reset*/
#define  ES7210_CLOCK_OFF_REG01             0x01        /* 用于关闭ADC时钟 */
#define  ES7210_MAINCLK_REG02               0x02        /* 设置ADC时钟分频 */
#define  ES7210_MASTER_CLK_REG03            0x03        /* MCLK源和SCLK分频 */
#define  ES7210_LRCK_DIVH_REG04             0x04        /* LRCK分频高位 */
#define  ES7210_LRCK_DIVL_REG05             0x05        /* LRCK分频低位 */
#define  ES7210_POWER_DOWN_REG06            0x06        /* 电源关闭 */
#define  ES7210_OSR_REG07                   0x07
#define  ES7210_MODE_CONFIG_REG08           0x08        /* 设置主从模式和通道数 */
#define  ES7210_TIME_CONTROL0_REG09         0x09        /* 设置芯片初始化状态周期 */
#define  ES7210_TIME_CONTROL1_REG0A         0x0A        /* 设置上电状态周期 */
#define  ES7210_SDP_INTERFACE1_REG11        0x11        /* 设置采样和格式 */
#define  ES7210_SDP_INTERFACE2_REG12        0x12        /* 引脚状态 */

#define  ES7210_ADC_AUTOMUTE_REG13          0x13        /* 设置静音 */
#define  ES7210_ADC34_MUTERANGE_REG14       0x14        /* 设置静音范围 */
#define  ES7210_ALC_SEL_REG16               0x16        /* 设置ALC模式 */
#define  ES7210_ADC1_DIRECT_DB_REG1B        0x1B
#define  ES7210_ADC2_DIRECT_DB_REG1C        0x1C
#define  ES7210_ADC3_DIRECT_DB_REG1D        0x1D
#define  ES7210_ADC4_DIRECT_DB_REG1E        0x1E        /* ALC关闭时为ADC直接dB，ALC打开时为最大增益 */
#define  ES7210_ADC34_HPF2_REG20            0x20        /* 高通滤波器 */
#define  ES7210_ADC34_HPF1_REG21            0x21
#define  ES7210_ADC12_HPF2_REG22            0x22
#define  ES7210_ADC12_HPF1_REG23            0x23
#define  ES7210_ANALOG_REG40                0x40        /* 模拟电源 */

#define  ES7210_MIC12_BIAS_REG41            0x41
#define  ES7210_MIC34_BIAS_REG42            0x42
#define  ES7210_MIC1_GAIN_REG43             0x43
#define  ES7210_MIC2_GAIN_REG44             0x44
#define  ES7210_MIC3_GAIN_REG45             0x45
#define  ES7210_MIC4_GAIN_REG46             0x46
#define  ES7210_MIC1_POWER_REG47            0x47
#define  ES7210_MIC2_POWER_REG48            0x48
#define  ES7210_MIC3_POWER_REG49            0x49
#define  ES7210_MIC4_POWER_REG4A            0x4A
#define  ES7210_MIC12_POWER_REG4B           0x4B        /* MIC偏置、ADC和PGA电源 */
#define  ES7210_MIC34_POWER_REG4C           0x4C

/* ═══════════════════════════════════════════════════════════════
 * ES7210 寄存器地址
 * ═══════════════════════════════════════════════════════════════ */
#define ES7210_CHIP_ID_REG3F                0x3F
#define ES7210_DMIC_FREQ_REG0E              0x0E
#define ES7210_DMIC_CONFIG_REG10            0x10
#define ES7210_ADC12_MUTE_REG15             0x15
#define ES7210_REG0B                        0x0B
#define ES7210_REG0D                        0x0D
#define ES7210_REG0F                        0x0F

typedef enum {
    ES7210_I2S_FMT_I2S   = 0x00,
    ES7210_I2S_FMT_LJ    = 0x01,
    ES7210_I2S_FMT_DSP_A = 0x03,
    ES7210_I2S_FMT_DSP_B = 0x13
} es7210_i2s_fmt_t;

typedef enum {
    ES7210_I2S_BITS_16B = 16,
    ES7210_I2S_BITS_18B = 18,
    ES7210_I2S_BITS_20B = 20,
    ES7210_I2S_BITS_24B = 24,
    ES7210_I2S_BITS_32B = 32
} es7210_i2s_bits_t;

typedef enum {
    ES7210_MIC_GAIN_0DB  = 0,
    ES7210_MIC_GAIN_3DB  = 1,
    ES7210_MIC_GAIN_6DB  = 2,
    ES7210_MIC_GAIN_9DB  = 3,
    ES7210_MIC_GAIN_12DB = 4,
    ES7210_MIC_GAIN_15DB = 5,
    ES7210_MIC_GAIN_18DB = 6,
    ES7210_MIC_GAIN_21DB = 7,
    ES7210_MIC_GAIN_24DB = 8,
    ES7210_MIC_GAIN_27DB = 9,
    ES7210_MIC_GAIN_30DB = 10,
    ES7210_MIC_GAIN_33DB = 11,
    ES7210_MIC_GAIN_34_5DB = 12,
    ES7210_MIC_GAIN_36DB = 13,
    ES7210_MIC_GAIN_37_5DB = 14
} es7210_mic_gain_t;

typedef enum {
    ES7210_MIC_BIAS_2V18 = 0x00,
    ES7210_MIC_BIAS_2V26 = 0x10,
    ES7210_MIC_BIAS_2V36 = 0x20,
    ES7210_MIC_BIAS_2V45 = 0x30,
    ES7210_MIC_BIAS_2V55 = 0x40,
    ES7210_MIC_BIAS_2V66 = 0x50,
    ES7210_MIC_BIAS_2V78 = 0x60,
    ES7210_MIC_BIAS_2V87 = 0x70
} es7210_mic_bias_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint32_t mclk_ratio;
    es7210_i2s_fmt_t i2s_format;
    es7210_i2s_bits_t bit_width;
    es7210_mic_bias_t mic_bias; 
    es7210_mic_gain_t mic_gain; 
    struct {
        uint32_t tdm_enable: 1; 
    } flags;
} es7210_codec_config_t;


typedef struct {
    i2c_master_bus_handle_t i2c_bus;      /* I2C 总线句柄 */
    int mclk_io;                          /* MCLK 引脚 */
    int bclk_io;                          /* BCLK 引脚 */
    int ws_io;                            /* LRCK/WS 引脚 */
    int din_io;                           /* DIN 引脚 (接 ES7210 SDOUT) */
    uint32_t sample_rate;                 /* 采样率 (Hz), 如 16000 */
    uint8_t  mic_mask;                    /* 麦克风通道掩码 */
    uint8_t  pga_gain;                    /* 模拟 PGA 增益 (0~14, 每档 3dB) */
    uint8_t  total_slots;                 /* I2S 总 slot 数 (2=STD立体声, 4=TDM四通道) */
} es7210_drv_config_t;

/**
 * @brief  初始化 ES7210 + I2S 并上电
 *
 * 时序: I2S 使能(产生 MCLK) → I2C 配置 ES7210 寄存器 → 上电
 *
 * @param cfg    配置参数
 * @param handle 输出句柄
 * @return ESP_OK 成功, 其他为错误码
 */
esp_err_t es7210_drv_init(const es7210_drv_config_t *cfg, es7210_drv_handle_t *handle);

/**
 * @brief  读取 PCM 采样数据
 *
 * @param handle  句柄
 * @param buf     输出缓冲区 (int16_t, TDM: slot0,slot1,...交错)
 * @param samples 期望读取的帧数 (每帧 = total_slots 个采样)
 * @return 实际读取的帧数, <0 表示错误
 */
int es7210_drv_read(es7210_drv_handle_t handle, int16_t *buf, int samples);

/**
 * @brief  打印 ES7210 全部寄存器值 (调试用)
 */
void es7210_drv_dump_regs(es7210_drv_handle_t handle);

/**
 * @brief  下电并释放 ES7210 + I2S 资源
 */
esp_err_t es7210_drv_deinit(es7210_drv_handle_t handle);

#ifdef __cplusplus
}
#endif
