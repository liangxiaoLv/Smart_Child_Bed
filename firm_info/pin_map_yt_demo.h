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
