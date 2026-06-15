#include "wifi_connect.h"
#include "mic_sample.h"
#include "mm_wave.h"
#include "sleep_monitor.h"
#include "rgb_led.h"
#include "rgb_screen_16_16_4.h"

#include "red_temp.h"
#include "i2c_driver.h"
#include "aw9523b_driver.h"
#include "uart_driver.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sntp.h"
#include <time.h>


#include "pin_map.h"


static const char *TAG = "main";
#if 0


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

        // rgbScreen_renderFrame(ens210_getLatestTemp(),
        //                       ens160_getLatestAQI(),
        //                       ti.tm_hour, ti.tm_min, ti.tm_sec);
        rgbScreen_renderFrame(28,
                              0,
                              ti.tm_hour, ti.tm_min, ti.tm_sec);
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(500));
    }
}



#endif

void app_main(void)
{
    /* 初始化 I2C 总线（集中管理） */
    i2c_master_bus_handle_t i2c0_bus;
    ESP_ERROR_CHECK(i2cDriver_initBus(I2C0_PORT_NUM, I2C0_SDA_PIN, I2C0_SCL_PIN, &i2c0_bus));

    /*初始化IO扩展芯片并点亮LED*/
    ESP_ERROR_CHECK(aw9523bDriver_init(i2c0_bus, NULL));
    /* P0.0 接 LED, 设为输出, 拉低点亮 */
    ESP_ERROR_CHECK(aw9523bDriver_setDir(AW_PIN_P00, AW_PIN_OUT));
    ESP_ERROR_CHECK(aw9523bDriver_setPin(AW_PIN_P00, AW_PIN_LOW));
    ESP_LOGI(TAG, "AW9523B P0.0 LED lighted");

    /*连接wifi（内部注册 IP_EVENT_STA_GOT_IP → 自动启 MQTT + 云端上报）*/
    wifiConnect_init();

    // // uart1 使用体温传感器
    // uartDriver_switch_device(UART1_PORT_NUM, UART1_DEVICE_IRTEMP);
    // IRTemp_start();

    // // uart2 使用毫米波雷达
    // uartDriver_switch_device(UART2_PORT_NUM, UART2_DEVICE_RADAR);
    // mm_wave_radar_info();


    
    
    // micSample_start(i2c0_bus);



    // rgbScreen16x16x4_init(); 
    // rgbScreen16x16x4_initButtons();   /* KEY0/1/2 中断 */
#if 0
    /* 初始化 音频设备*/
    wavPlayer_init(i2c0_bus);
    

    /* 2. 注册 WiFi 就绪回调（必须在 wifiConnect_init 之后，事件循环已存在）
     *    用于 BLE 配网后重连场景 */
    

    // /* 3. 直连模式下 WiFi 已就绪，直接启动 MQTT（内部防重入） */
    


    /* 6. 启动灯带 */
    rgbLed_work(i2c0_bus);
    // SENSOR_CONTROL_RGB(i2c0_bus);
    rgbScreen_init();
    rgbScreen16x16x4_init();   /* 16×16×4 大屏: 启动显示 ABCD, key1 翻页 */

    // /* 7. 启动传感器模块 */
    // mm_wave_radar_info();

    // /* BCG 睡眠监护仪 */
    // ESP_ERROR_CHECK(sleepMonitor_init());
    // vTaskDelay(pdMS_TO_TICKS(500));
    // sleepMonitor_setAutoReportMode();

    
    // ens210_temp_info(i2c1_bus);
    // ens160_info(i2c1_bus);

    /* 8. 初始化 NTP 时间同步 */
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();

    /* 9. 启动点阵屏显示任务 */
    xTaskCreate(displayTask, "rgb_disp", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "系统初始化完成");
#endif
}
