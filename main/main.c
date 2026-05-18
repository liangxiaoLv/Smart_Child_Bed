#include "wifi_connect.h"
#include "cloud_mqtt.h"
#include "trans_2_cloud.h"
#include "ens210.h"
#include "ens160.h"
#include "mm_wave.h"
#include "rgb_led.h"
#include "wav_player.h"
#include "xl9555_driver.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

extern const uint8_t start_connect_wav_start[] asm("_binary_start_connect_wav_start");
extern const uint8_t start_connect_wav_end[]   asm("_binary_start_connect_wav_end");
extern const uint8_t wifi_connect_ok_wav_start[] asm("_binary_wifi_connect_ok_wav_start");
extern const uint8_t wifi_connect_ok_wav_end[]   asm("_binary_wifi_connect_ok_wav_end");

/* WiFi 获取到 IP 后自动启动 MQTT 和云端上报 */
static void onGotIP(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ESP_LOGI(TAG, "WiFi 已就绪，启动 MQTT + 云端上报");
    size_t len = wifi_connect_ok_wav_end - wifi_connect_ok_wav_start;
    wavPlayer_play(wifi_connect_ok_wav_start, len);
    mqttClient_start();
    trans2cloud_start();
}

void app_main(void)
{
    /* 1. WiFi 连接（内部创建事件循环） */
    wifiConnect_init();

    /* 2. 注册 WiFi 就绪回调（必须在 wifiConnect_init 之后，事件循环已存在）
     *    用于 BLE 配网后重连场景 */
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onGotIP, NULL);

    /* 3. 直连模式下 WiFi 已就绪，直接启动 MQTT（内部防重入） */
    mqttClient_start();
    trans2cloud_start();

    /* 4. 启动灯带 */
    rgbLed_work();

    /* 5. 启动传感器模块 */
    mm_wave_radar_info();
    ens210_temp_info();
    ens160_info();

    ESP_LOGI(TAG, "系统初始化完成");
}
