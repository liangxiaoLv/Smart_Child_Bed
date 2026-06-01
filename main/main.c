#include "wifi_connect.h"
#include "cloud_mqtt.h"
#include "trans_2_cloud.h"
#include "ens210.h"
#include "ens160.h"
#include "mm_wave.h"
#include "rgb_led.h"
#include "rgb_screen_8_32.h"
#include "sensor_rgb.h"
#include "wav_player.h"
#include "xl9555_driver.h"
#include "es8388_driver.h"
#include "red_temp.h"
#include "i2c_driver.h"
#include "pin_map.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <time.h>

static const char *TAG = "main";

extern const uint8_t start_connect_wav_start[] asm("_binary_start_connect_wav_start");
extern const uint8_t start_connect_wav_end[]   asm("_binary_start_connect_wav_end");
extern const uint8_t wifi_connect_ok_wav_start[] asm("_binary_wifi_connect_ok_wav_start");
extern const uint8_t wifi_connect_ok_wav_end[]   asm("_binary_wifi_connect_ok_wav_end");

/* 点阵屏显示刷新任务（500ms 一帧，冒号 1Hz 闪烁） */
static void displayTask(void *arg)
{
    /* 等待 NTP 同步，最长 15 秒 */
    time_t now;
    for (int i = 0; i < 150; i++) {
        time(&now);
        if (now > 1700000000) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        time(&now);
        struct tm ti;
        localtime_r(&now, &ti);

        rgbScreen_renderFrame(ens210_getLatestTemp(),
                              ens160_getLatestAQI(),
                              ti.tm_hour, ti.tm_min, ti.tm_sec);

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
    }
}

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

    /* 初始化 I2C 总线（集中管理） */
    i2c_master_bus_handle_t i2c0_bus, i2c1_bus;
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C0_PORT_NUM, I2C0_SDA_PIN, I2C0_SCL_PIN, &i2c0_bus));
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C1_PORT_NUM, I2C1_SDA_PIN, I2C1_SCL_PIN, &i2c1_bus));
    // /* 1. WiFi 连接（内部创建事件循环） */
    // wifiConnect_init(i2c0_bus);

    // /* 2. 注册 WiFi 就绪回调（必须在 wifiConnect_init 之后，事件循环已存在）
    //  *    用于 BLE 配网后重连场景 */
    // esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onGotIP, NULL);

    // /* 3. 直连模式下 WiFi 已就绪，直接启动 MQTT（内部防重入） */
    // mqttClient_start();
    // trans2cloud_start();

    // /* 5. 初始化 I2C0 设备 */
    // xl9555Driver_init(i2c0_bus);
    // es8388Driver_init(i2c0_bus);

    /* 6. 启动灯带 */
    rgbLed_work(i2c0_bus);
    SENSOR_CONTROL_RGB(i2c0_bus);
    // rgbScreen_init();

    // /* 7. 启动传感器模块 */
    // mm_wave_radar_info();
    // redTemp_start();
    // ens210_temp_info(i2c1_bus);
    // ens160_info(i2c1_bus);

    // /* 8. 初始化 NTP 时间同步 */
    // setenv("TZ", "CST-8", 1);
    // tzset();
    // esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // esp_sntp_setservername(0, "ntp.aliyun.com");
    // esp_sntp_init();

    // /* 9. 启动点阵屏显示任务 */
    // xTaskCreate(displayTask, "rgb_disp", 3072, NULL, 2, NULL);

    // ESP_LOGI(TAG, "系统初始化完成");
}
