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
| — | GPIO 39 | GPIO 38 | GT9XX 触摸 (也挂 I2C1) |

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

## Flash 分区

| 分区 | 偏移 | 大小 | 用途 |
|------|------|------|------|
| factory | 0x10000 | 896KB | 出厂固件 |
| ota_0 | 0xF0000 | 896KB | OTA slot A |
| ota_1 | 0x1D0000 | 896KB | OTA slot B |

## BLE + WiFi 配网

### 依赖清单

**ESP-IDF 组件 (CMakeLists.txt REQUIRES):**

| 组件 | 用途 |
|------|------|
| `wifi_provisioning` | 统一配网框架核心，状态机 + 凭证管理 |
| `protocomm` | 协议通信层，BLE 传输 + 安全握手 |
| `protobuf-c` | 配网协议序列化 (Google protobuf) |
| `bt` 或 `nimble` | BLE 协议栈 (推荐 NimBLE，省内存) |
| `nvs_flash` | WiFi 凭证持久化存储 |
| `esp_wifi` | WiFi STA 驱动 |
| `esp_netif` | lwIP 网络接口管理 |
| `cJSON` | JSON 解析 (MQTT 上行/下行) |
| `esp_mqtt` | MQTT 客户端 (配网后连接云端) |

**移动端 App:**

| 平台 | App |
|------|-----|
| Android / iOS | ESP BLE Provisioning (官方) |
| 微信小程序 | 基于 Web Bluetooth API 自研 |

**模块依赖链:**
```
prov_manager.c
  ├── drivers/wifi/wifi.c          → esp_wifi 封装
  ├── drivers/button/button.c      → KEY1 长按触发重配网
  ├── app/ui_display/ui_wifi.c     → LCD 显示 PIN / 状态 / RSSI
  ├── wifi_provisioning/manager.h  → ESP-IDF 配网框架头文件
  └── wifi_provisioning/scheme_ble.h → BLE 传输方案
```

### 整体流程

**启动 → 分支判断:**
```
上电 → NVS Flash 初始化 → 读取 NVS 中 WiFi 凭证
                                    │
                       ┌────────────┼────────────┐
                      有凭证                   无凭证
                       │                         │
                       ▼                         ▼
                 路径A: WiFi直连            路径B: BLE配网
```

**路径 A — 已配网 (WiFi 直连):**
```
WiFi STA 初始化 → esp_wifi_connect() 用 NVS 凭证
                         │
                    ┌────┴────┐
                   成功      失败(N次重试后)
                    │           │
                    ▼           ▼
               获取 IP     切路径B: 进入 BLE 配网
                    │
                    ▼
             MQTT 连接 Broker
                    │
                    ▼
          订阅下行主题 + 心跳 → 正常运转
```

**路径 B — BLE 配网:**
```
Device 侧                          Mobile 侧
────────                           ────────
启动 BLE 广播
服务名: "PROV_xxxx"
LCD 显示 POP PIN 码                扫描 BLE 设备
       │                               │
       ▼                               ▼
等待连接 ────────────────────────→ 连接设备
       │                               │
       ▼                               ▼
Security 1 握手 ←── PIN 验证 ───→ 输入 LCD 上的 PIN
       │                               │
       ▼                               ▼
收到扫描请求 ←──────────────────── 发起 WiFi 扫描
→ 扫描周边 AP                        │
→ 返回 AP 列表 ──────────────────→ 显示 AP 列表
       │                               │
       ▼                               ▼
收到 WiFi 凭证 ←────────────────── 用户选 AP 输密码
(SSID + Password)                      │
       │                               │
       ▼                               │
写入 NVS → 连接 WiFi                    │
       │                               │
  成功 → 停止 BLE 广播                 │
  失败 → 报告手机，重试                 │
       │                               │
       ▼                               ▼
进入路径A (MQTT + 正常运转)
```

**重配网 (KEY1 长按 3秒):**
```
KEY1 长按 → 擦除 NVS 凭证 → esp_wifi_disconnect() → 重新进入路径B
```

### 全局调用序列 (Network_Task 内部)

```
1. wifi_init()                     // NVS + lwIP + WiFi STA 初始化
2. prov_init()                     // 注册按键回调, LCD 显示 PIN
3. if (prov_is_provisioned()):
     wifi_connect(NULL, NULL, 5)   // 用 NVS 凭证连接, 最多5次
     → 成功: mqtt_client_init() + mqtt_client_start()
     → 失败: prov_start()          // 进入 BLE 配网
   else:
     prov_start()                  // 直接 BLE 配网
4. 循环: mqtt_publish_heartbeat()  // 每30秒
```

## 软件分层架构

```
app/          FreeRTOS 任务 + LVGL UI + 云端指令处理 + 日志
services/     数据采集 / BCG体征解析 / 电机控制 / 场景联动 / OTA / 功耗 / 看门狗
middleware/   FreeRTOS配置 / WiFi&BLE / MQTT / LVGL适配 / JSON / OTA / 电机协议
drivers/      传感器 / BCG / 电机 / 显示屏 / 灯带 / 扬声器 / 按键 / 雷达 / XL9555
bsp/          GPIO / I2C / UART / SPI / PWM / I2S / ADC / Timer
```

横切:
- **platform/**: msg_bus (消息总线) + state_mgr (共享状态) + event_handler
- **config/**: pinmap, thresholds, mqtt_topics
- **common/**: 类型定义, 错误码, 工具函数

**依赖规则:** 上层依赖下层；同层模块通过 msg_bus 通信，禁止直接依赖；所有层可依赖 config/ 和 common/。

## RTOS 任务规划

| 任务 | 优先级 | 栈 | 功能 |
|------|--------|-----|------|
| System | 6 | 2K | 看门狗、低电量、模式切换 |
| Motor | 5 | 3K | 电机控制、堵转/限位保护 |
| BCG | 4 | 4K | UART 收 BCG 帧、心率呼吸解析 |
| Sensor | 3 | 4K | I2C 周期采集传感器 |
| Network | 3 | 8K | WiFi、MQTT、心跳 |
| Display | 2 | 4K | LVGL 驱动 LCD |
| Audio | 2 | 4K | 扬声器 / TTS |
| Button | 2 | 2K | 按键消抖、长短按 |
| OTA | 2 | 8K | 固件下载、校验、分区切换 |
| Light | 1 | 2K | 灯带 PWM、自适应亮度 |

## 编码规范

- **命名**: 驼峰式 camelCase
- **注释**: 不写啰嗦注释，代码自解释优先
- **错误处理**: 关键操作重试3次，致命错误触发 esp_restart()

## 交互约定

- 先讨论方案再写代码，不要直接动手
- 缺失的硬件信息直接问，不要猜
- 优先修改现有文件，避免新建
- 引脚、外设地址等硬件常量统一放 `config/pinmap/`

## 编译

统一告诉用户，用户自行编译
