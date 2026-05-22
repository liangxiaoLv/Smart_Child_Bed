#include "sensor_control.h"
#include "ap3216c_driver.h"
#include "rgb_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_ctrl";

#define TASK_INTERVAL_MS    500
#define BRIGHTNESS_MIN       10
#define BRIGHTNESS_MAX      100
#define ALS_MAX            65535

static void sensorControlTask(void *arg)
{
    for (;;) {
        uint16_t als = ap3216cDriver_readALS();
        uint16_t ps  = ap3216cDriver_readPS();
        uint16_t ir  = ap3216cDriver_readIR();

        int brt = BRIGHTNESS_MAX - (int)(((uint32_t)als * (BRIGHTNESS_MAX - BRIGHTNESS_MIN)) / ALS_MAX);
        if (brt < BRIGHTNESS_MIN) brt = BRIGHTNESS_MIN;
        if (brt > BRIGHTNESS_MAX) brt = BRIGHTNESS_MAX;

        rgbLed_setBrightness((uint8_t)brt);

        ESP_LOGI(TAG, "ALS=%5u PS=%5u IR=%5u → 亮度=%d%%", als, ps, ir, brt);
        vTaskDelay(pdMS_TO_TICKS(TASK_INTERVAL_MS));
    }
}

esp_err_t SENSOR_CONTROL_RGB(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ap3216cDriver_init(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP3216C 初始化失败");
        return ret;
    }

    xTaskCreate(sensorControlTask, "sensor_ctrl", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "环境光→RGB 自动控制已启动");
    return ESP_OK;
}
