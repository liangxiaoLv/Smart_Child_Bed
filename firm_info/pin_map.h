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

#define ESP_IDF_version "v6.0.0"
// #define YT_ESP32_DEMO_BOARD
#define DNESP32_BOARD

/* ═══════════════════════════════════════════════════════════════
 * UART0 — 串口调试 / 固件下载
 YT DEMO接专用TXD0 RXD0不占用IO口
 * ═══════════════════════════════════════════════════════════════ */
#ifdef DNESP32_BOARD 
#define UART0_TX_PIN        GPIO_NUM_0      /* UART0 发送引脚 */
#define UART0_RX_PIN        GPIO_NUM_1      /* UART0 接收引脚 */
#define UART0_BAUD_RATE     115200          /* 调试串口波特率 */
#endif

#define UART1_DEVICE_IRTEMP 1
#define UART1_DEVICE_MOTO   0

#define UART2_DEVICE_BCG    1
#define UART2_DEVICE_RADAR  0

#define UART1_PORT_NUM 1
#define UART1_TX_PIN        GPIO_NUM_17     /* UART1 发送引脚 */
#define UART1_RX_PIN        GPIO_NUM_18     /* UART1 接收引脚 */

#define UART2_PORT_NUM 2
#define UART2_TX_PIN        GPIO_NUM_42      /* UART2 发送引脚 */
#define UART2_RX_PIN        GPIO_NUM_41      /* UART2 接收引脚 */


/* ═══════════════════════════════════════════════════════════════
 * I2C0 — XL9555/AW9523B  IO扩展芯片
 * ═══════════════════════════════════════════════════════════════ */
#define I2C0_PORT_NUM       0               /* I2C 端口号，对应 I2C_NUM_0 */
#ifdef DNESP32_BOARD 
#define I2C0_SDA_PIN        GPIO_NUM_4     /* I2C0 数据线 */
#define I2C0_SCL_PIN        GPIO_NUM_5     /* I2C0 时钟线 */
#else
#define I2C0_SDA_PIN        GPIO_NUM_1     /* I2C0 数据线 */
#define I2C0_SCL_PIN        GPIO_NUM_2     /* I2C0 时钟线 */
#endif
#define I2C0_SPEED_HZ       100000          /* I2C0 总线速率 100 kHz */
/* ═══════════════════════════════════════════════════════════════
 * I2C1
 * ═══════════════════════════════════════════════════════════════ */
#define I2C1_PORT_NUM       1                /* I2C 端口号，对应 I2C_NUM_1 */
#define I2C1_SCL_PIN        GPIO_NUM_15      /* I2C1 时钟线，与 LCD_DE / RADAR_TX 复用 */
#define I2C1_SDA_PIN        GPIO_NUM_16      /* I2C1 数据线，与 LCD_PCLK / RADAR_RX 复用 */
#define I2C1_SPEED_HZ       100000           /* I2C1 总线速率 100 kHz */

/* ═══════════════════════════════════════════════════════════════
 * SPI2 — LCD 数据总线 / TF 卡
 * TF 卡与 LCD 共用 SPI2，通过 CS 片选区分
 * DNESP32_BOARD使用
 * ═══════════════════════════════════════════════════════════════ */
#define SPI2_MOSI_PIN       GPIO_NUM_11     /* SPI2 主出从入 */
#define SPI2_CLK_PIN        GPIO_NUM_12     /* SPI2 时钟 */
#define SPI2_MISO_PIN       GPIO_NUM_13     /* SPI2 主入从出 */
#define SPI2_HOST_ID        SPI2_HOST       /* SPI2 外设 ID */

/* TF 卡 — SPI2 设备 */
#define SD_CARD_CS_PIN      GPIO_NUM_2      /* TF 卡 SPI 片选 */
/* ═══════════════════════════════════════════════════════════════
 * XL9555/AW9523B — IO 扩展芯片（挂载于 I2C0）
 * ═══════════════════════════════════════════════════════════════ */
#define XL9555_I2C_ADDR     0x20            /* XL9555 7-bit I2C 设备地址 */
#define XL9555_INT_PIN      GPIO_NUM_40     /* XL9555 中断输出引脚 */

#define AW9523B_I2C_ADDR    0x5B            /* AW9523B 7-bit I2C 设备地址 */
#define AW9523B_INTN_PIN    GPIO_NUM_47     /* 中断输出引脚, 低电平有效 */
#define AW9523B_RSTN_PIN    GPIO_NUM_48     /* 复位引脚, 低电平复位 */

/* AW9523B 端口引脚 — UART 设备切换 */
#define UART1_SW_PIN        AW_PIN_P14      /* UART1 切换: P1_4, 高=DeviceA, 低=DeviceB */
#define UART2_SW_PIN        AW_PIN_P15      /* UART2 切换: P1_5, 高=DeviceA, 低=DeviceB */
/* ═══════════════════════════════════════════════════════════════
 * mmWave 毫米波雷达 — UART1
 * 注意：GPIO 4/5 与 I2C1、LCD 控制线复用
 * ═══════════════════════════════════════════════════════════════ */
#define RADAR_UART_NUM      UART2_PORT_NUM          
#define RADAR_RX_PIN        UART2_RX_PIN      /* 雷达 UART 接收 */
#define RADAR_TX_PIN        UART2_TX_PIN      /* 雷达 UART 发送 */
#define RADAR_BAUD_RATE     9600            /* 雷达串口波特率 */
#define RADAR_RX_BUF_SIZE   2048            /* 雷达 UART RX 缓冲区 */

/* ═══════════════════════════════════════════════════════════════
 * BCG 睡眠监护仪 — 已停用（UART2 现归红外体温传感器）
 * 如需重新启用，需分配空闲 UART 或与其它设备共用
 * ═══════════════════════════════════════════════════════════════ */
#define BCG_UART_NUM        UART2_PORT_NUM   
#define BCG_RX_PIN          UART2_RX_PIN 
#define BCG_TX_PIN          UART2_TX_PIN    
#define BCG_BAUD_RATE       115200          /* BCG 串口波特率 */
#define BCG_RX_BUF_SIZE     4096            /* BCG UART RX 缓冲区 (AD 采样帧最大 ~1025B) */

/* ═══════════════════════════════════════════════════════════════
 * 红外体温传感器 — UART2
 * ═══════════════════════════════════════════════════════════════ */
#define RED_UART_NUM        UART1_PORT_NUM  /* 红外使用 UART1 */
#define RED_RX_PIN          UART1_RX_PIN      /* 传感器上的TX */
#define RED_TX_PIN          UART1_TX_PIN      /* 传感器上的RX */
#define RED_BAUD_RATE       921600          /* 红外串口波特率 */
#define RED_RX_BUF_SIZE     20480           /* 红外 UART RX 缓冲区（一帧 10256B） */

/* ═══════════════════════════════════════════════════════════════
 * ENS210温度传感器 — I2C1
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
 * I2S0 — 预留（原 ES8388，驱动保留但不使用）
 * ═══════════════════════════════════════════════════════════════ */
#ifdef DNESP32_BOARD 
#define I2S_MCLK_PIN        GPIO_NUM_3      /* I2S0_MCK，与 LCD_G5 复用 */
#define I2S_BCLK_PIN        GPIO_NUM_46     /* I2S0_SCK，与 LCD_G4 复用 */
#define I2S_WS_PIN          GPIO_NUM_9      /* I2S0_LRCK，与 LCD_G3 复用 */
#define I2S_DOUT_PIN        GPIO_NUM_47     /* I2S0_DOUT → ES8388 SDIN (原理图 IO47) */
#define I2S_DIN_PIN         GPIO_NUM_10     /* I2S0_DIN ← ES7210 SDOUT1 (原理图 IO10) */
#define I2S_PORT_NUM        0               /* I2S 端口号 */
#endif
/* ═══════════════════════════════════════════════════════════════
 * I2S1 — AW88399QNR 音频功放
 * ═══════════════════════════════════════════════════════════════ */
#define I2S1_BCK_PIN        GPIO_NUM_4      /* I2S1 位时钟 */
#define I2S1_LRCK_PIN       GPIO_NUM_5      /* I2S1 字选 (LRCK) */
#define I2S1_DOUT_PIN       GPIO_NUM_6      /* I2S1 数据输出（ESP32 → AW88399 DATAI） */
#define I2S1_DIN_PIN        GPIO_NUM_7      /* I2S1 数据输入（AW88399 DATAO → ESP32，喇叭回采） */
#define I2S1_PORT_NUM       1

/* ═══════════════════════════════════════════════════════════════
 * AW88399QNR — 音频功放（挂载于 I2C0）
 * ═══════════════════════════════════════════════════════════════ */
#define AW88399QNR_I2C_ADDR 0x34            /* 7-bit I2C 地址 (ADDR脚接GND) */
#define AW88399_PA_RSTN_PIN  GPIO_NUM_8     /* 功放复位, 低电平有效 */

/* ═══════════════════════════════════════════════════════════════
 * WS2812 RGB 灯带 — RMT
 * 数据线 GPIO 45
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_LED_DATA_PIN     GPIO_NUM_13      /* WS2812 数据线 */
#define RGB_LED_NUM      120              /* 灯带 LED 数量 */
#define RGB_LED_RMT_RES_HZ   10000000        /* RMT 分辨率 10MHz */

/* ═══════════════════════════════════════════════════════════════
 * RGB 8×32 点阵屏 — RMT (WS2812 协议)
 * 数据线 GPIO 39
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_SCREEN_8_32_DATA_PIN     GPIO_NUM_39
#define RGB_SCREEN_8_32_LED_NUM      256              /* 256 颗实际显示 */
#define RGB_SCREEN_8_32_RMT_RES_HZ   10000000        /* RMT 分辨率 10MHz */

/* ═══════════════════════════════════════════════════════════════
 * RGB 16×16×4 大屏 (4 块 16×16 横向拼接, 1024 LED)
 * 数据线 GPIO 5
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_SCREEN_16_16_4_DATA_PIN     GPIO_NUM_45
#define RGB_SCREEN_16_16_4_LED_NUM      1024
#define RGB_SCREEN_16_16_4_RMT_RES_HZ   10000000

/* ═══════════════════════════════════════════════════════════════
 * 16×16×4 大屏 — 功能按键 (KEY0=绿, KEY1=黄, KEY2=红)
 * 按下低电平, 需上拉
 * ═══════════════════════════════════════════════════════════════ */
#define RGB_SCREEN_KEY0_PIN     GPIO_NUM_0      /* 绿色 - 翻页 */
#define RGB_SCREEN_KEY1_PIN     GPIO_NUM_47      /* 黄色 - 亮度+10% */
#define RGB_SCREEN_KEY2_PIN     GPIO_NUM_45      /* 红色 - 亮度-10% */

/* ═══════════════════════════════════════════════════════════════
 * 旋转编码器 — 正交脉冲 + 按键
 * V → 3.3V, G → GND
 * ═══════════════════════════════════════════════════════════════ */
#define ROTARY_ENC_A_PIN     GPIO_NUM_39     /* A 相脉冲信号 */
#define ROTARY_ENC_B_PIN     GPIO_NUM_38     /* B 相脉冲信号 */
#define ROTARY_ENC_SW_PIN    GPIO_NUM_40      /* 按键信号（按下为低） */

/* ═══════════════════════════════════════════════════════════════
 * ES7210 — 4 通道音频 ADC (挂载于 I2C0)
 * 3 路 MIC 采集, I2S 输出
 * ═══════════════════════════════════════════════════════════════ */
#define ES7210_I2C_ADDR      0x80           /* 8-bit I2C 地址 (esp_codec_dev 内部会 >>1 转 7-bit)，AD1=AD0=0 */
#define ES7210_I2S_MCLK_PIN  GPIO_NUM_3     /* 主时钟 */
#define ES7210_I2S_BCLK_PIN  GPIO_NUM_46    /* 位时钟 */
#define ES7210_I2S_LRCK_PIN  GPIO_NUM_9     /* 左右声道时钟 */
#define ES7210_I2S_DIN_PIN   GPIO_NUM_14    /* ADC 数据 (ES7210 SDOUT1 → ESP32 DIN) */
#define ES7210_I2S_DIN_ALT_PIN  GPIO_NUM_10    /* 原理图备选 IO10 */
#define ES7210_INT_PIN       GPIO_NUM_12    /* 中断输出 */

