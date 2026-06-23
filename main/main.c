#include "wifi_connect.h"
#include "mic_sample.h"
#include "mm_wave.h"
#include "sleep_monitor.h"
#include "rgb_led.h"
#include "rgb_screen_16_16_4.h"
#include "classify_tflite_service.h"

#include "red_temp.h"
#include "i2c_driver.h"
#include "aw9523b_driver.h"
#include "uart_driver.h"
#include "rotary_encoder.h"
#include "wav_player.h"
#include "aw883xx_driver.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <time.h>
#include <stdio.h>


#include "pin_map.h"


static const char *TAG = "main";

#define I2C0_SCAN_ADDR_START 0x30
#define I2C0_SCAN_ADDR_END   0x5C
#define I2C0_SCAN_TIMEOUT_MS 50

static void i2c0_scan_devices(i2c_master_bus_handle_t bus)
{
    int found_count = 0;
    char found_addrs[256] = {0};
    size_t offset = 0;

    ESP_LOGI(TAG, "I2C0 scan start: 0x%02X-0x%02X",
             I2C0_SCAN_ADDR_START, I2C0_SCAN_ADDR_END);

    for (uint8_t addr = I2C0_SCAN_ADDR_START; addr <= I2C0_SCAN_ADDR_END; addr++) {
        esp_err_t ret = i2cDriver_probe(bus, addr, I2C0_SCAN_TIMEOUT_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C0 device found at 0x%02X", addr);
            int written = snprintf(found_addrs + offset, sizeof(found_addrs) - offset,
                                   "%s0x%02X", found_count ? ", " : "", addr);
            if (written > 0) {
                size_t remain = sizeof(found_addrs) - offset;
                offset += (written < remain) ? written : remain - 1;
            }
            found_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGI(TAG, "I2C0 scan done, found %d device(s)", found_count);
    ESP_LOGI(TAG, "I2C0 devices: %s", found_count ? found_addrs : "none");
}
static void micSampleTask(void *arg)
{
    micSample_start_classify(arg);
}

/* 点阵屏时间显示任务，500ms 刷一帧，冒号 1Hz 闪烁 */
static void displayTask(void *arg)
{
    /* 等待 NTP 同步，最长 15 秒 */
    time_t now;
    for (int i = 0; i < 150; i++) {
        time(&now);
        if (now > 1700000000) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    bool colon = true;
    TickType_t lastWake = xTaskGetTickCount();
    for (;;) {
        time(&now);
        struct tm ti;
        localtime_r(&now, &ti);
        rgbScreen16x16x4_showTime(ti.tm_hour, ti.tm_min, ti.tm_sec, colon);
        colon = !colon;
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    // /* 初始化 I2C 总线（集中管理） */
    i2c_master_bus_handle_t i2c0_bus;
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C0_PORT_NUM, I2C0_SDA_PIN, I2C0_SCL_PIN, &i2c0_bus));

    i2c_master_bus_handle_t i2c1_bus;
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C1_PORT_NUM, I2C1_SDA_PIN, I2C1_SCL_PIN, &i2c1_bus));

    // /*初始化IO扩展芯片并点亮板上LED*/
    // ESP_ERROR_CHECK(aw9523bDriver_init(i2c0_bus, NULL));
    // /* P0.0 接 LED, 设为输出, 拉低点亮 */
    // ESP_ERROR_CHECK(aw9523bDriver_setDir(AW_PIN_P00, AW_PIN_OUT));
    // ESP_ERROR_CHECK(aw9523bDriver_setPin(AW_PIN_P00, AW_PIN_LOW));
    ESP_LOGI(TAG, "AW9523B P0.0 LED lighted");
#if 0
    /* 初始化音频功放 AW88399QNR（I2C0, addr=0x40） */
    esp_err_t wav_err = wavPlayer_init(i2c0_bus);
    if (wav_err != ESP_OK) {
        ESP_LOGE(TAG, "wavPlayer_init 失败: %s，音频功能不可用", esp_err_to_name(wav_err));
    } else {
        /* I2C 写入方式诊断：自动测试 12 种寄存器读写方法 */
        aw883xx_testWrites();
        /* 诊断完成后播正弦波验证 */
        wavPlayer_testTone(440, 2000, 32000);
    }
#endif
    /* RGB 灯带 + 旋转编码器（WS2812 IO13，旋钮调亮度，按键开关） */
    rgbLed_init();
    /* 麦克风采样与 WiFi 并行: 先启 I2S, 避免等 WiFi 期间时钟未输出 */
    xTaskCreate(micSampleTask, "mic_sample", 8192, i2c0_bus, 5, NULL);
    /*连接wifi（内部注册 IP_EVENT_STA_GOT_IP → 自动启 MQTT + 云端上报）*/
    wifiConnect_init();


    // uart1 使用体温传感器
    uartDriver_switch_device(UART1_PORT_NUM, UART1_DEVICE_IRTEMP);
    IRTemp_start();
    // uart2 BCG睡眠监测
    uartDriver_switch_device(UART2_PORT_NUM, UART2_DEVICE_BCG);
    ESP_ERROR_CHECK(sleepMonitor_init());
    vTaskDelay(pdMS_TO_TICKS(500));
    sleepMonitor_setAutoReportMode(); /*切换为自动上报模式*/


    rgbScreen16x16x4_init();
    /* NTP 时间同步（东八区） */
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();

    /* 启动时间显示任务 */
    xTaskCreate(displayTask, "rgb_time", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "系统初始化完成");

}
