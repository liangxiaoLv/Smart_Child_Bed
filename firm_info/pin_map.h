#pragma once

#include "soc/gpio_num.h"    /* gpio_num_t, GPIO_NUM_xx */

/*
 * pin_map.h — 全局硬件引脚与外设地址映射
 * 芯片: ESP32-S3FH4R2
 *
 * 所有 GPIO 编号、外设地址、总线参数统一在此定义。
 * 其他模块需要硬件信息时 #include "pin_map.h"，禁止在各驱动内部散落定义。
 *
 * 注意：GPIO 4/5/40 存在运行时复用冲突，使用前需按场景切换 GPIO 模式。
 */

/* ═══════════════════════════════════════════════════════════════
 * UART0 — 调试 / 固件下载
 * ═══════════════════════════════════════════════════════════════ */
#define UART0_TX_PIN        GPIO_NUM_0      /* UART0 发送引脚 */
#define UART0_RX_PIN        GPIO_NUM_1      /* UART0 接收引脚 */
#define UART0_BAUD_RATE     115200          /* 调试串口波特率 */

/* ═══════════════════════════════════════════════════════════════
 * I2C0 — XL9555 IO 扩展芯片
 * ═══════════════════════════════════════════════════════════════ */
#define I2C0_PORT_NUM       0               /* I2C 端口号，对应 I2C_NUM_0 */
#define I2C0_SDA_PIN        GPIO_NUM_41     /* I2C0 数据线 */
#define I2C0_SCL_PIN        GPIO_NUM_42     /* I2C0 时钟线 */
#define I2C0_SPEED_HZ       100000          /* I2C0 总线速率 100 kHz */

/* ═══════════════════════════════════════════════════════════════
 * I2C1 — 光/温湿度/气体传感器
 * 注意：GPIO 4/5 与 LCD 控制线、毫米波雷达 UART 复用
 * 重映射：ENS210 使用时将 I2C1 映射到 GPIO 17/18
 * ═══════════════════════════════════════════════════════════════ */
#define I2C1_PORT_NUM       1               /* I2C 端口号，对应 I2C_NUM_1 */
#define I2C1_SDA_PIN        GPIO_NUM_5      /* I2C1 数据线，与 LCD_PCLK / RADAR_RX 复用 */
#define I2C1_SCL_PIN        GPIO_NUM_4      /* I2C1 时钟线，与 LCD_DE / RADAR_TX 复用 */
#define I2C1_SPEED_HZ       100000          /* I2C1 总线速率 100 kHz */

/* ═══════════════════════════════════════════════════════════════
 * SPI2 — LCD 数据总线（或预留）
 * ═══════════════════════════════════════════════════════════════ */
#define SPI2_MOSI_PIN       GPIO_NUM_11     /* SPI2 主出从入 */
#define SPI2_CLK_PIN        GPIO_NUM_12     /* SPI2 时钟 */
#define SPI2_MISO_PIN       GPIO_NUM_13     /* SPI2 主入从出 */

/* ═══════════════════════════════════════════════════════════════
 * XL9555 — IO 扩展芯片（挂载于 I2C0）
 * ═══════════════════════════════════════════════════════════════ */
#define XL9555_I2C_ADDR     0x20            /* XL9555 7-bit I2C 设备地址 */
#define XL9555_INT_PIN      GPIO_NUM_40     /* XL9555 中断输出引脚，与 GT9XX_INT / LCD_DC 复用 */

/* ═══════════════════════════════════════════════════════════════
 * LCD — ATK-MD0430（4.3 寸 800×480 RGB565）
 * ═══════════════════════════════════════════════════════════════ */

/* 控制信号 */
#define LCD_PCLK_PIN        GPIO_NUM_5      /* 像素时钟 18 MHz，下降沿有效，与 I2C1_SDA / RADAR_RX 复用 */
#define LCD_DE_PIN          GPIO_NUM_4      /* 数据使能，与 I2C1_SCL / RADAR_TX 复用 */
#define LCD_DC_PIN          GPIO_NUM_40     /* 数据/命令选择，与 XL9555_INT / GT9XX_INT 复用 */
/* LCD 复位  → XL9555 P1.2 (SLCD_RST)                          */
/* LCD 背光  → XL9555 P1.0 (LCD_BL)                            */
/* LCD 电源  → XL9555 P1.3 (SLCD_PWR)                          */

/* 16bit 数据线（RGB565 位序：B[4:0] G[5:0] R[4:0]） */
#define LCD_B3_PIN          GPIO_NUM_17     /* 蓝色数据位 B3 */
#define LCD_B4_PIN          GPIO_NUM_16     /* 蓝色数据位 B4 */
#define LCD_B5_PIN          GPIO_NUM_15     /* 蓝色数据位 B5 */
#define LCD_B6_PIN          GPIO_NUM_7      /* 蓝色数据位 B6 */
#define LCD_B7_PIN          GPIO_NUM_6      /* 蓝色数据位 B7 */
#define LCD_G2_PIN          GPIO_NUM_10     /* 绿色数据位 G2 */
#define LCD_G3_PIN          GPIO_NUM_9      /* 绿色数据位 G3 */
#define LCD_G4_PIN          GPIO_NUM_46     /* 绿色数据位 G4 */
#define LCD_G5_PIN          GPIO_NUM_3      /* 绿色数据位 G5 */
#define LCD_G6_PIN          GPIO_NUM_8      /* 绿色数据位 G6 */
#define LCD_G7_PIN          GPIO_NUM_18     /* 绿色数据位 G7 */
#define LCD_R3_PIN          GPIO_NUM_45     /* 红色数据位 R3 */
#define LCD_R4_PIN          GPIO_NUM_48     /* 红色数据位 R4 */
#define LCD_R5_PIN          GPIO_NUM_47     /* 红色数据位 R5 */
#define LCD_R6_PIN          GPIO_NUM_21     /* 红色数据位 R6 */
#define LCD_R7_PIN          GPIO_NUM_14     /* 红色数据位 R7 */

/* 分辨率与时序参数 */
#define LCD_H_RES                   800         /* 水平分辨率（像素） */
#define LCD_V_RES                   480         /* 垂直分辨率（像素） */
#define LCD_PCLK_HZ                 18000000    /* 像素时钟频率 18 MHz */
#define LCD_HSYNC_BACK_PORCH        48          /* 水平同步后沿 */
#define LCD_HSYNC_FRONT_PORCH       88          /* 水平同步前沿 */
#define LCD_HSYNC_PULSE_WIDTH       40          /* 水平同步脉冲宽度 */
#define LCD_VSYNC_BACK_PORCH        3           /* 垂直同步后沿 */
#define LCD_VSYNC_FRONT_PORCH       32          /* 垂直同步前沿 */
#define LCD_VSYNC_PULSE_WIDTH       13          /* 垂直同步脉冲宽度 */

/* ═══════════════════════════════════════════════════════════════
 * GT9XX — 触摸控制器
 * ═══════════════════════════════════════════════════════════════ */
#define GT9XX_SDA_PIN       GPIO_NUM_39     /* GT9XX I2C 数据线 */
#define GT9XX_SCL_PIN       GPIO_NUM_38     /* GT9XX I2C 时钟线 */
#define GT9XX_INT_PIN       GPIO_NUM_40     /* GT9XX 中断引脚，与 XL9555_INT / LCD_DC 复用 */
/* GT9XX 复位 → XL9555 P1.1 (CT_RST) */

/* ═══════════════════════════════════════════════════════════════
 * mmWave 毫米波雷达 — UART1
 * 注意：GPIO 4/5 与 I2C1、LCD 控制线复用
 * ═══════════════════════════════════════════════════════════════ */
#define RADAR_UART_NUM      1               /* 雷达使用 UART1 */
#define RADAR_RX_PIN        GPIO_NUM_5      /* 雷达 UART 接收，与 I2C1_SDA / LCD_PCLK 复用 */
#define RADAR_TX_PIN        GPIO_NUM_4      /* 雷达 UART 发送，与 I2C1_SCL / LCD_DE 复用 */
#define RADAR_BAUD_RATE     9600            /* 雷达串口波特率 */

/* ═══════════════════════════════════════════════════════════════
 * 红外体温传感器 — UART1
 * 注意：GPIO 4/5 与 I2C1、LCD 控制线复用
 * ═══════════════════════════════════════════════════════════════ */
#define RED_UART_NUM        1               /* 红外使用 UART1 */
#define RED_RX_PIN          GPIO_NUM_5      /* 红外 UART 接收，与 I2C1_SDA / LCD_PCLK 复用 */
#define RED_TX_PIN          GPIO_NUM_4      /* 红外 UART 发送，与 I2C1_SCL / LCD_DE 复用 */
#define RED_BAUD_RATE       115200          /* 红外串口波特率 */

/* ═══════════════════════════════════════════════════════════════
 * ENS210温度传感器 — I2C1（重映射）
 * SDA/SCL 使用 GPIO 17/18，与 LCD 数据线 B3/G7 复用
 * ═══════════════════════════════════════════════════════════════ */
#define ENS210_SDA_PIN      GPIO_NUM_17     /* ENS210 I2C 数据线 */
#define ENS210_SCL_PIN      GPIO_NUM_18     /* ENS210 I2C 时钟线 */
#define ENS210_I2C_ADDR     0x43            /* ENS210 7-bit I2C 设备地址 */
