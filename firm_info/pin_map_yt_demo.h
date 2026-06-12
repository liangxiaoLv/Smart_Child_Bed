#pragma once

#include "soc/gpio_num.h"

/*
 * pin_map_yt_demo.h — YT 演示项目引脚映射
 * 与 pin_map.h (主项目) 并存; 仅 YT 演示项目 #include 此文件
 *
 * 当前规划:
 *   I2C0 IO 扩展芯片改为 AW9523B (XL9555 已停用, 代码保留不调用)
 */

/* ═══════════════════════════════════════════════════════════════
 * I2C0 — AW9523B IO 扩展芯片
 * 与主项目 (XL9555 用 IO41/IO42) 不同, YT 演示项目用 IO1/IO2
 * ═══════════════════════════════════════════════════════════════ */
#define I2C0_PORT_NUM       0
#define I2C0_SDA_PIN        GPIO_NUM_1      /* I2C0 数据线 (YT 演示) */
#define I2C0_SCL_PIN        GPIO_NUM_2      /* I2C0 时钟线 (YT 演示) */
#define I2C0_SPEED_HZ       100000          /* I2C0 总线速率 100 kHz */

/* ═══════════════════════════════════════════════════════════════
 * AW9523B — IO 扩展芯片 (挂载于 I2C0)
 * 16 通道通用 GPIO 扩展
 * ═══════════════════════════════════════════════════════════════ */
#define AW9523B_I2C_ADDR    0x5B            /* AW9523B 7-bit I2C 设备地址 */
#define AW9523B_INTN_PIN    GPIO_NUM_47     /* 中断输出引脚, 低电平有效 */
#define AW9523B_RSTN_PIN    GPIO_NUM_48     /* 复位引脚, 低电平复位 */

/* ═══════════════════════════════════════════════════════════════
 * ES7210 — 4 通道音频 ADC (挂载于 I2C0)
 * 3 路 MIC 采集, I2S 输出
 * ═══════════════════════════════════════════════════════════════ */
#define ES7210_I2C_ADDR      0x80           /* 8-bit I2C 地址 (esp_codec_dev 内部会 >>1 转 7-bit)，AD1=AD0=0 */
#define ES7210_I2S_MCLK_PIN  GPIO_NUM_3     /* 主时钟 */
#define ES7210_I2S_BCLK_PIN  GPIO_NUM_46    /* 位时钟 */
#define ES7210_I2S_LRCK_PIN  GPIO_NUM_9     /* 左右声道时钟 */
#define ES7210_I2S_DIN_PIN   GPIO_NUM_10    /* ADC 数据 (ES7210 DOUT → ESP32 DIN) */
#define ES7210_INT_PIN       GPIO_NUM_12    /* 中断输出 */
