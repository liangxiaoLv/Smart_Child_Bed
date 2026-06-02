/**
 * wifi_connect.c — WiFi STA + BLE 配网
 *
 * 基于 ESP-IDF network_provisioning 框架:
 *   - 已配网: WiFi 直连
 *   - 未配网: BLE 广播 → 手机 ESP BLE Provisioning App 配网
 *
 * 安全: Security 2 (SRP6a + AES-GCM), 开发模式硬编码 salt/verifier
 * 手机端: 用户名 "wifiprov", 密码 "abcd1234"
 */

#include "wifi_connect.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

#include <string.h>
#include <stdio.h>

/* ─── 常量 ─────────────────────────────────────────────────── */

static const char *TAG = "wifi_connect";

#define PROV_SERVICE_PREFIX   "PROV_"
#define MAX_RETRY             5
#define BOOT_BUTTON_PIN       GPIO_NUM_0       /* BOOT 按键 GPIO */
#define BOOT_LONG_PRESS_MS    3000             /* 长按阈值 (ms) */
#define STATUS_LED_PIN         GPIO_NUM_1       /* 状态 LED，低电平点亮 */

/* Sec2 开发模式硬编码 salt/verifier (username="wifiprov", pwd="abcd1234")
 * 生产环境每个设备需生成唯一值，存储在出厂分区 */
static const char s_sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8, 0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};

static const char s_sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};

/* ─── 模块状态 ─────────────────────────────────────────────── */

static EventGroupHandle_t s_wifi_eg = NULL;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static int s_retry = 0;

/* ─── BOOT 按键长按检测 ──────────────────────────────────────── */

static esp_timer_handle_t s_boot_btn_timer = NULL;

static void bootBtnTimerCB(void *arg)
{
    ESP_LOGW(TAG, "BOOT button held %dms — clearing WiFi credentials and restarting...",
             BOOT_LONG_PRESS_MS);
    gpio_set_level(STATUS_LED_PIN, 0);  /* LED 点亮，提示进入配网 */
    wifiConnect_clearCredentials();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void IRAM_ATTR bootBtnISR(void *arg)
{
    if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
        /* 按下: 启动长按定时器 */
        esp_timer_start_once(s_boot_btn_timer, BOOT_LONG_PRESS_MS * 1000);
    } else {
        /* 松开: 取消定时器 */
        esp_timer_stop(s_boot_btn_timer);
    }
}

static void bootBtnInit(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(BOOT_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);

    const esp_timer_create_args_t timer_args = {
        .callback = bootBtnTimerCB,
        .name = "boot_btn",
    };
    esp_timer_create(&timer_args, &s_boot_btn_timer);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BOOT_BUTTON_PIN, bootBtnISR, NULL);

    ESP_LOGI(TAG, "BOOT button monitor: hold GPIO%d for %dms to reset WiFi credentials",
             BOOT_BUTTON_PIN, BOOT_LONG_PRESS_MS);
}

/* ─── 状态 LED (IO1, 低电平点亮) ──────────────────────────────── */

static void statusLedInit(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(STATUS_LED_PIN, 1);  /* 默认熄灭 */
}

/* ─── 生成 BLE 设备名 ──────────────────────────────────────── */

static void getDeviceServiceName(char *name, size_t max)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(name, max, "%s%02X%02X%02X",
             PROV_SERVICE_PREFIX, mac[3], mac[4], mac[5]);
}

/* ─── 事件处理 ─────────────────────────────────────────────── */

static void provEventHandler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "Provisioning service started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)data;
            ESP_LOGI(TAG, "Received WiFi credentials — SSID: %s", cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            network_prov_wifi_sta_fail_reason_t *reason =
                (network_prov_wifi_sta_fail_reason_t *)data;
            ESP_LOGE(TAG, "Provisioning failed: %s",
                     *reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR ?
                     "Auth error" : "AP not found");
            network_prov_mgr_reset_wifi_sm_state_on_failure();
            break;
        }
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning success, WiFi connected");
            gpio_set_level(STATUS_LED_PIN, 1);  /* LED 熄灭 */
            xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
            break;
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "Provisioning service stopped, restarting...");
            network_prov_mgr_deinit();
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;
        default:
            break;
        }
    } else if (base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (id) {
        case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
            ESP_LOGI(TAG, "BLE connected");
            break;
        case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
            ESP_LOGI(TAG, "BLE disconnected");
            break;
        default:
            break;
        }
    } else if (base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "Secure session established");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGE(TAG, "Username/password verification failed");
            break;
        default:
            break;
        }
    }
}

static esp_timer_handle_t s_retry_timer = NULL;

static void wifiSlowRetryCB(void *arg)
{
    s_retry = 0;
    esp_wifi_connect();
}

static void wifiEventHandler(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry < MAX_RETRY) {
            esp_wifi_connect();
            s_retry++;
            ESP_LOGW(TAG, "WiFi reconnecting (%d/%d)", s_retry, MAX_RETRY);
        } else {
            ESP_LOGW(TAG, "WiFi retry exhausted, will retry in 30s");
            if (!s_retry_timer) {
                const esp_timer_create_args_t tcfg = {
                    .callback = wifiSlowRetryCB,
                    .name = "wifi_slow",
                };
                esp_timer_create(&tcfg, &s_retry_timer);
            }
            esp_timer_start_once(s_retry_timer, 30 * 1000 * 1000);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;

        wifi_config_t wifi_cfg;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

        ESP_LOGI(TAG, "══════ WiFi Connected ══════");
        ESP_LOGI(TAG, "  SSID:    %s", wifi_cfg.sta.ssid);
        ESP_LOGI(TAG, "  IP:      " IPSTR, IP2STR(&ev->ip_info.ip));
        ESP_LOGI(TAG, "  Netmask: " IPSTR, IP2STR(&ev->ip_info.netmask));
        ESP_LOGI(TAG, "  Gateway: " IPSTR, IP2STR(&ev->ip_info.gw));
        ESP_LOGI(TAG, "══════════════════════════");

        s_retry = 0;
        gpio_set_level(STATUS_LED_PIN, 1);  /* LED 熄灭 */
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
    }
}

/* ─── 公共 API ─────────────────────────────────────────────── */

bool wifiConnect_isProvisioned(void)
{
    bool provisioned = false;
    network_prov_mgr_is_wifi_provisioned(&provisioned);
    return provisioned;
}

esp_err_t wifiConnect_clearCredentials(void)
{
    ESP_LOGI(TAG, "Clear WiFi credentials");
    return network_prov_mgr_reset_wifi_provisioning();
}

esp_err_t wifiConnect_init(void)
{
    /* 1. NVS 初始化 */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    /* 1.5 启动 BOOT 按键监控 (长按清除 WiFi 凭据) */
    statusLedInit();
    bootBtnInit();

    /* 2. 网络栈初始化 */
    s_wifi_eg = xEventGroupCreate();
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop creation failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "WiFi init failed");

    /* 3. Register event handlers */
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                   provEventHandler, NULL),
        TAG, "Register NETWORK_PROV event failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID,
                                   provEventHandler, NULL),
        TAG, "Register BLE event failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID,
                                   provEventHandler, NULL),
        TAG, "Register security event failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifiEventHandler, NULL),
        TAG, "Register WiFi event failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   wifiEventHandler, NULL),
        TAG, "Register IP event failed");

    /* 4. 初始化配网管理器 */
    network_prov_mgr_config_t prov_cfg = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(prov_cfg), TAG, "Provisioning manager init failed");

    /* 5. 检查是否已配网 */
    bool provisioned = false;
    network_prov_mgr_is_wifi_provisioned(&provisioned);

    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned, connecting WiFi directly...");
        network_prov_mgr_deinit();
        esp_event_handler_unregister(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, provEventHandler);
        esp_event_handler_unregister(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, provEventHandler);
        esp_event_handler_unregister(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, provEventHandler);

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "WiFi mode set failed");
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "WiFi start failed");

        EventBits_t bits = xEventGroupWaitBits(s_wifi_eg,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
            pdMS_TO_TICKS(15000));

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "WiFi direct connection failed (%d retries), will keep retrying", s_retry);
        return ESP_OK;  /* 已配网但连不上，让慢速重试定时器继续工作，不启动BLE配网 */
    }

    /* 6. 启动 BLE 配网 (Security 2) — 仅在未配网时执行 */
    gpio_set_level(STATUS_LED_PIN, 0);  /* LED 点亮，指示配网中 */
    char service_name[16];
    getDeviceServiceName(service_name, sizeof(service_name));
    ESP_LOGI(TAG, "Starting BLE provisioning — service name: %s (Security 2)", service_name);

    network_prov_security2_params_t sec2_params = {
        .salt      = s_sec2_salt,
        .salt_len  = sizeof(s_sec2_salt),
        .verifier  = s_sec2_verifier,
        .verifier_len = sizeof(s_sec2_verifier),
    };

    ESP_RETURN_ON_ERROR(
        network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_2,
                                            &sec2_params,
                                            service_name,
                                            NULL),
        TAG, "BLE provisioning start failed");

    return ESP_OK;
}
