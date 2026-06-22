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
#include "aw88399qnr_driver.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <time.h>


#include "pin_map.h"


static const char *TAG = "main";


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
    /* 初始化 I2C 总线（集中管理） */
    i2c_master_bus_handle_t i2c0_bus;
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C0_PORT_NUM, I2C0_SDA_PIN, I2C0_SCL_PIN, &i2c0_bus));

    // /* 初始化 I2C1 总线 */

    
    /*初始化IO扩展芯片并点亮LED*/
    ESP_ERROR_CHECK(aw9523bDriver_init(i2c0_bus, NULL));
    /* P0.0 接 LED, 设为输出, 拉低点亮 */
    ESP_ERROR_CHECK(aw9523bDriver_setDir(AW_PIN_P00, AW_PIN_OUT));
    ESP_ERROR_CHECK(aw9523bDriver_setPin(AW_PIN_P00, AW_PIN_LOW));
    ESP_LOGI(TAG, "AW9523B P0.0 LED lighted");
#if 0
    /* 初始化音频功放 AW88399QNR（I2C0, addr=0x40） */
    esp_err_t wav_err = wavPlayer_init(i2c0_bus);
    if (wav_err != ESP_OK) {
        ESP_LOGE(TAG, "wavPlayer_init 失败: %s，音频功能不可用", esp_err_to_name(wav_err));
    } else {
        /* I2C 写入方式诊断：自动测试 12 种寄存器读写方法 */
        aw88399qnr_testWrites();
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


    // // uart1 使用体温传感器
    // uartDriver_switch_device(UART1_PORT_NUM, UART1_DEVICE_IRTEMP);
    // IRTemp_start();
    // /* BCG 睡眠监护仪 */
    // uartDriver_switch_device(UART2_PORT_NUM, UART2_DEVICE_BCG);
    // ESP_ERROR_CHECK(sleepMonitor_init());
    // vTaskDelay(pdMS_TO_TICKS(500));
    // sleepMonitor_setAutoReportMode();
#if 0
    // uart2 使用毫米波雷达
    uartDriver_switch_device(UART2_PORT_NUM, UART2_DEVICE_RADAR);
    mm_wave_radar_info();
#endif
    // rgbScreen16x16x4_init();
    // /* NTP 时间同步（东八区） */
    // setenv("TZ", "CST-8", 1);
    // tzset();
    // esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // esp_sntp_setservername(0, "ntp.aliyun.com");
    // esp_sntp_init();

    // /* 启动时间显示任务 */
    // xTaskCreate(displayTask, "rgb_time", 3072, NULL, 2, NULL);

    // ESP_LOGI(TAG, "系统初始化完成");

}
