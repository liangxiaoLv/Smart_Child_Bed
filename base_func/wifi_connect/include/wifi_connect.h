#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>

/**
 * 初始化 WiFi STA + BLE 配网模块。
 *
 * @param bus  I2C0 总线句柄（用于 KEY0 长按监测的 XL9555）
 *
 * 流程：
 *  - NVS 已有凭证 → WiFi 直连
 *  - NVS 无凭证   → 启动 BLE 广播，等待手机 ESP BLE Provisioning App 配网
 *
 * 同时启动 KEY0 长按监测任务（>3s 清除凭证并重启，进入重新配网）。
 */
esp_err_t wifiConnect_init(i2c_master_bus_handle_t bus);

/** 检查 NVS 中是否已存有 WiFi 凭证 */
bool wifiConnect_isProvisioned(void);

/** 清除 NVS 中的 WiFi 凭证 */
esp_err_t wifiConnect_clearCredentials(void);
