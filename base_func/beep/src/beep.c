#include "beep.h"
#include "xl9555_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "beep";

static void beepTask(void *arg)
{
    for (;;) {
        xl9555Driver_setPin(XL9555_PORT0, XL9555_BEEP, false);
        vTaskDelay(pdMS_TO_TICKS(500));
        xl9555Driver_setPin(XL9555_PORT0, XL9555_BEEP, true);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t beep_work(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = xl9555Driver_init(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "XL9555 初始化失败");
        return ret;
    }

    xTaskCreate(beepTask, "beep", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "蜂鸣器任务已启动");
    return ESP_OK;
}
