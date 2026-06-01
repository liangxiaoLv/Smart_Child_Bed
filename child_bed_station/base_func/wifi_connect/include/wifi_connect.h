#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * 初始化 WiFi STA + BLE 配网模块。
 *
 * 流程：
 *  - NVS 已有凭证 → WiFi 直连
 *  - NVS 无凭证   → 启动 BLE 广播，等待手机 ESP BLE Provisioning App 配网
 */
esp_err_t wifiConnect_init(void);

/** 检查 NVS 中是否已存有 WiFi 凭证 */
bool wifiConnect_isProvisioned(void);

/** 清除 NVS 中的 WiFi 凭证 */
esp_err_t wifiConnect_clearCredentials(void);
