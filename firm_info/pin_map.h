#pragma once

#include "soc/gpio_num.h"    /* gpio_num_t, GPIO_NUM_xx */

/*
 * pin_map.h — 全局硬件引脚与外设地址映射
 * 芯片: ESP32-S3FH4R2
 *
 * 所有 GPIO 编号、外设地址、总线参数统一在此定义。
 * 其他模块需要硬件信息时 #include "pin_map.h"，禁止在各驱动内部散落定义。
 *
 * 注意：GPIO 存在运行时复用冲突，使用前需按场景切换 GPIO 模式。
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
#define I2C1_SDA_PIN        GPIO_NUM_17      /* I2C1 数据线，与 LCD_PCLK / RADAR_RX 复用 */
#define I2C1_SCL_PIN        GPIO_NUM_18      /* I2C1 时钟线，与 LCD_DE / RADAR_TX 复用 */
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
 * mmWave 毫米波雷达 — UART1
 * 注意：GPIO 4/5 与 I2C1、LCD 控制线复用
 * ═══════════════════════════════════════════════════════════════ */
#define RADAR_UART_NUM      2               /* 雷达使用 UART2 */
#define RADAR_RX_PIN        GPIO_NUM_5      /* 雷达 UART 接收 */
#define RADAR_TX_PIN        GPIO_NUM_4      /* 雷达 UART 发送 */
#define RADAR_BAUD_RATE     9600            /* 雷达串口波特率 */
#define RADAR_RX_BUF_SIZE   2048            /* 雷达 UART RX 缓冲区 */

/* ═══════════════════════════════════════════════════════════════
 * 红外体温传感器 — UART1
 * 注意：GPIO 4/5 与 I2C1、LCD 控制线复用
 * ═══════════════════════════════════════════════════════════════ */
#define RED_UART_NUM        1               /* 红外使用 UART1 */
#define RED_RX_PIN          GPIO_NUM_6      /* 红外 UART 接收*/
#define RED_TX_PIN          GPIO_NUM_7      /* 红外 UART 发送 */
#define RED_BAUD_RATE       921600          /* 红外串口波特率 */
#define RED_RX_BUF_SIZE     20480           /* 红外 UART RX 缓冲区（一帧 10256B） */

/* ═══════════════════════════════════════════════════════════════
 * ENS210温度传感器 — I2C1（重映射）
 * SDA/SCL 使用 GPIO 17/18，与 LCD 数据线 B3/G7 复用
 * ═══════════════════════════════════════════════════════════════ */
#define ENS210_SDA_PIN      GPIO_NUM_17     /* ENS210 I2C 数据线 */
#define ENS210_SCL_PIN      GPIO_NUM_18     /* ENS210 I2C 时钟线 */
#define ENS210_I2C_ADDR     0x43            /* ENS210 7-bit I2C 设备地址 */

/* ═══════════════════════════════════════════════════════════════
 * ENS160气体传感器 — I2C1（重映射）
 * SDA/SCL 使用 GPIO 17/18，与 LCD 数据线 B3/G7 复用
 * ═══════════════════════════════════════════════════════════════ */
#define ENS160_SDA_PIN      GPIO_NUM_17     /* ENS160 I2C 数据线 */
#define ENS160_SCL_PIN      GPIO_NUM_18     /* ENS160 I2C 时钟线 */
#define ENS160_I2C_ADDR     0x52            /* ENS160 7-bit I2C 设备地址（MISO/ADDR 低电平） */

/* ═══════════════════════════════════════════════════════════════
 * ES8388 — 音频编解码芯片（挂载于 I2C0）
 * ═══════════════════════════════════════════════════════════════ */
#define ES8388_I2C_ADDR     0x10            /* ES8388 7-bit I2C 设备地址 */

/* ─── AP3216C — 环境光/接近传感器（挂载于 I2C0）──────────────── */
#define AP3216C_I2C_ADDR    0x1E            /* AP3216C 7-bit I2C 设备地址 */

/* ═══════════════════════════════════════════════════════════════
 * I2S — 音频
 * 复用 LCD 数据线
 * ═══════════════════════════════════════════════════════════════ */
#define I2S_MCLK_PIN        GPIO_NUM_3      /* I2S 主时钟，与 LCD_G5 复用 */
#define I2S_BCLK_PIN        GPIO_NUM_46     /* I2S 位时钟 (SCK)，与 LCD_G4 复用 */
#define I2S_WS_PIN          GPIO_NUM_9      /* I2S 字选 (LRCK)，与 LCD_G3 复用 */
#define I2S_DOUT_PIN        GPIO_NUM_10     /* I2S 数据输出 (接 ES8388 SDIN)，与 LCD_G2 复用 */
#define I2S_DIN_PIN         GPIO_NUM_14     /* I2S 数据输入 (接 ES8388 SDOUT)，与 LCD_R7 复用 */
#define I2S_PORT_NUM        0               /* I2S 端口号 */

/* ═══════════════════════════════════════════════════════════════
 * WS2812 RGB 灯带 — RMT
 * 数据线 GPIO 45
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_LED_DATA_PIN     GPIO_NUM_38      /* WS2812 数据线 */
#define RGB_LED_NUM      15              /* 灯带 LED 数量 */
#define RGB_LED_RMT_RES_HZ   10000000        /* RMT 分辨率 10MHz */

/* ═══════════════════════════════════════════════════════════════
 * RGB 8×32 点阵屏 — RMT (WS2812 协议)
 * 数据线 GPIO 48，与 LCD_R4 复用
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_SCREEN_DATA_PIN     GPIO_NUM_39
#define RGB_SCREEN_LED_NUM      256              /* 256 颗实际显示 */
#define RGB_SCREEN_RMT_RES_HZ   10000000        /* RMT 分辨率 10MHz */

/* ═══════════════════════════════════════════════════════════════
 * 旋转编码器 — 正交脉冲 + 按键
 * V → 3.3V, G → GND
 * ═══════════════════════════════════════════════════════════════ */
#define ROTARY_ENC_A_PIN     GPIO_NUM_17     /* A 相脉冲信号 */
#define ROTARY_ENC_B_PIN     GPIO_NUM_16     /* B 相脉冲信号 */
#define ROTARY_ENC_SW_PIN    GPIO_NUM_6      /* 按键信号（按下为低） */