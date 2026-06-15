/**
 ****************************************************************************************************
 * @file        es7210.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       es7210驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32S3 BOX3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#ifndef __ES7210_H_
#define __ES7210_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_types.h"
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/i2s.h"
#include "driver/i2s_tdm.h"
#include "driver/i2c_master.h"
#include "myiic.h"
#include "math.h"
#include "string.h"
#include "myi2s.h"
#include "es8311.h"

/* ES7210的IIC通信地址 */
#define  ES7210_ADDRRES                      0x40

/* 常用寄存器声明 */
#define  ES7210_RESET_REG00                 0x00        /* 复位控制 */
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

/* 声明函数 */
esp_err_t es7210_config_codec(const es7210_codec_config_t *codec_conf);
esp_err_t es7210_config_volume(int8_t volume_db);
void es7210_init(bool is_tdm);

#endif
