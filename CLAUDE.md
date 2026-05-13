# CLAUDE.md

## 项目概述

智能儿童床嵌入式软件系统，基于 ESP32-S3FH4R2 重新开发。

- **芯片**: ESP32-S3FH4R2, 双核 Xtensa LX7 @ 240MHz
- **内存**: 512KB SRAM + 8MB PSRAM + 16MB Flash (QIO)
- **框架**: ESP-IDF v6.0 (`C:\esp\v6.0\esp-idf`)
- **串口**: 115200, 目标 `esp32s3`

## 硬件平台

### I2C 总线




| 总线 | SDA | SCL | 设备 |
|------|-----|-----|------|
| I2C0 | GPIO 41 | GPIO 42 | XL9555 IO 扩展 (addr 0x20) |
| I2C1 | GPIO 5 | GPIO 4 | 光/温湿度/气体传感器 |

### SPI2 (LCD 用，或预留)

| 信号 | GPIO |
|------|------|
| MOSI | 11 |
| CLK | 12 |
| MISO | 13 |

### XL9555 IO 扩展芯片 (I2C0, addr 0x20, INT → GPIO 40)

```
位15                                                          位0
P1.7  P1.6  P1.5  P1.4  P1.3  P1.2  P1.1  P1.0   P0.7 P0.6 P0.5 P0.4 P0.3 P0.2 P0.1 P0.0
```

| 位 | 宏 | 方向 | 功能 |
|----|-----|------|------|
| P0.0 | `AP_INT_IO` | IN | AP 中断 |
| P0.1 | `QMA_INT_IO` | IN | QMA 中断 |
| P0.2 | `SPK_EN_IO` | OUT | 扬声器使能 (H=关) |
| P0.3 | `BEEP_IO` | OUT | 蜂鸣器 (H=关) |
| P0.4 | `OV_PWDN_IO` | OUT | OV 掉电 |
| P0.5 | `OV_RESET_IO` | OUT | OV 复位 |
| P0.6 | `GBC_LED_IO` | OUT | GBC LED |
| P0.7 | `GBC_KEY_IO` | IN | GBC 按键 |
| P1.0 | `LCD_BL_IO` | OUT | LCD 背光 |
| P1.1 | `CT_RST_IO` | OUT | 触摸复位 |
| P1.2 | `SLCD_RST_IO` | OUT | LCD 复位 |
| P1.3 | `SLCD_PWR_IO` | OUT | LCD 电源 |
| P1.4 | `KEY3_IO` | IN | 按键3 |
| P1.5 | `KEY2_IO` | IN | 按键2 |
| P1.6 | `KEY1_IO` | IN | 按键1 |
| P1.7 | `KEY0_IO` | IN | 按键0 |

> 上电配置寄存器写入 `0xF003`：P0.0/P0.1 输入(中断源)，P1.7-P1.4 输入(按键)，其余输出。SPK_EN 和 BEEP 初始拉高(关闭)。

### LCD — ATK-MD0430 (4.3寸 800x480, 16bit RGB565)

**控制信号:**
| 信号 | GPIO | 说明 |
|------|------|------|
| PCLK | GPIO 5 | 18MHz, 下降沿有效 |
| DE | GPIO 4 | 数据使能 |
| RST | XL9555 P1.2 | 复位 |
| 背光 | XL9555 P1.0 | |
| 电源 | XL9555 P1.3 | |
| DC | GPIO 40 | 数据/命令 (与 XL9555_INT 复用) |

**16bit 数据线 (RGB565 顺序 B0-B4, G0-G5, R0-R4):**
```
B3:17  B4:16  B5:15  B6:7  B7:6
G2:10  G3:9   G4:46  G5:3  G6:8  G7:18
R3:45  R4:48  R5:47  R6:21 R7:14
```

**时序:** H=800 V=480, HSYNC(48,88,40), VSYNC(3,32,13)

### 触摸 GT9XX

| SDA | SCL | INT | RST |
|-----|-----|-----|-----|
| GPIO 39 | GPIO 38 | GPIO 40 (复用) | XL9555 P1.1 |

### mmWave 雷达 (UART1)

| RX | TX | 波特率 |
|----|----|--------|
| GPIO 5 | GPIO 4 | 9600 |

### GPIO 复用冲突速查

| GPIO | 功能1 | 功能2 | 功能3 |
|------|-------|-------|-------|
| GPIO 4 | I2C1 SCL | LCD DE | RADAR TX |
| GPIO 5 | I2C1 SDA | LCD PCLK | RADAR RX |
| GPIO 40 | XL9555 INT | GT9XX INT | LCD DC |
| GPIO 21 | LCD R6 | (原 LCD CS) | |

> 这些引脚不能在同一个时刻用于多个功能，需要在运行时按场景切换 GPIO 模式。

### 全部 GPIO 占用清单

```
GPIO 0-1:  UART0 调试/下载
GPIO 3:    LCD G5
GPIO 4:    I2C1 SCL / LCD DE / RADAR TX
GPIO 5:    I2C1 SDA / LCD PCLK / RADAR RX
GPIO 6-10: LCD 数据线
GPIO 11-13: SPI2 (MOSI/CLK/MISO)
GPIO 14-18: LCD 数据线
GPIO 21:   LCD R6
GPIO 38-39: GT9XX I2C
GPIO 40:   XL9555 INT / GT9XX INT / LCD DC
GPIO 41-42: I2C0 (XL9555)
GPIO 45-48: LCD 数据线
```

## RTOS 任务规划

ht | 1 | 2K | 灯带 PWM、自适应亮度 |

## 编码规范

- **命名**: 驼峰式 camelCase
- **注释**: 不写啰嗦注释，代码自解释优先
- **错误处理**: 关键操作重试3次，致命错误触发 esp_restart()

## 交互约定

- 先讨论方案再写代码，不要直接动手
- 缺失的硬件信息直接问，不要猜或者编
- 优先修改现有文件，避免新建
- 引脚、外设地址等硬件常量统一放 `config/pinmap/`

## 编译

- 统一告诉用户，用户自行编译
- 缺少组件时提示用户自己下载

## attention
- ESP-IDF v6.0 将 I2C 等新驱动（i2c_master.h、i2c_new_master_bus 等）从 driver 组件拆分到了
  esp_driver_i2c等 组件