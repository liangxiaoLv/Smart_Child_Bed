#include "rotary_encoder.h"
#include "pin_map.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"

static const char *TAG = "rotary_enc";

#define POLL_INTERVAL_US      500
#define PRINT_INTERVAL_MS     500
#define DEBOUNCE_TICKS          3

static int32_t  s_encoderNum = 0;
static int      s_prevA;
static bool     s_button_pressed = false;

static void pollTimerCB(void *arg)
{
    int curA = gpio_get_level(ROTARY_ENC_A_PIN);
    if (curA != s_prevA) {
        int curB = gpio_get_level(ROTARY_ENC_B_PIN);
        if (curB != s_prevA) {
            s_encoderNum++;
        } else {
            s_encoderNum--;
        }
    }
    s_prevA = curA;
}

static void rotaryEncoderTask(void *arg)
{
    bool prevButton = true;
    int  btnStable = 0;
    TickType_t lastPrint = xTaskGetTickCount();

    for (;;) {
        bool raw = (gpio_get_level(ROTARY_ENC_SW_PIN) == 0);
        if (raw == prevButton) {
            btnStable = 0;
        } else {
            btnStable++;
            if (btnStable >= DEBOUNCE_TICKS) {
                prevButton = raw;
                btnStable = 0;
                s_button_pressed = raw;
                ESP_LOGI(TAG, "按键: %s", raw ? "按下" : "释放");
            }
        }

        if ((xTaskGetTickCount() - lastPrint) >= pdMS_TO_TICKS(PRINT_INTERVAL_MS)) {
            lastPrint = xTaskGetTickCount();
            ESP_LOGI(TAG, "编码器=%ld  按键=%s",
                     s_encoderNum / 2, s_button_pressed ? "按下" : "释放");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ROTARY_ENCODER_GET(void)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << ROTARY_ENC_A_PIN) |
                        (1ULL << ROTARY_ENC_B_PIN) |
                        (1ULL << ROTARY_ENC_SW_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    s_prevA = gpio_get_level(ROTARY_ENC_A_PIN);

    const esp_timer_create_args_t timer_cfg = {
        .callback = pollTimerCB,
        .name     = "enc_poll",
    };
    esp_timer_handle_t timer;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_cfg, &timer), TAG, "定时器创建失败");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(timer, POLL_INTERVAL_US), TAG, "定时器启动失败");

    xTaskCreate(rotaryEncoderTask, "rotary_enc", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "旋转编码器已启动");
    return ESP_OK;
}
