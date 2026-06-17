#include "buzzer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "buzzer";

#define BUZZER_PIN      GPIO_NUM_4

/* 低电平触发: 0=响, 1=停 */
#define BUZZER_ON       0
#define BUZZER_OFF      1

static volatile bool s_warning = false;

static void buzzerTask(void *arg)
{
    for (;;) {
        if (s_warning) {
            gpio_set_level(BUZZER_PIN, BUZZER_ON);
            vTaskDelay(pdMS_TO_TICKS(300));
            gpio_set_level(BUZZER_PIN, BUZZER_OFF);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            gpio_set_level(BUZZER_PIN, BUZZER_OFF);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t buzzer_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(BUZZER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_drive_capability(BUZZER_PIN, GPIO_DRIVE_CAP_3);
    gpio_set_level(BUZZER_PIN, BUZZER_OFF);

    gpio_set_level(BUZZER_PIN, BUZZER_OFF);

    xTaskCreate(buzzerTask, "buzzer", 1536, NULL, 1, NULL);
    ESP_LOGI(TAG, "Buzzer ready");
    return ESP_OK;
}

void buzzer_setWarning(bool on)
{
    s_warning = on;
    ESP_LOGI(TAG, "Warning %s", on ? "ON" : "OFF");
}
