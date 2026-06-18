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
#define DEBOUNCE_TICKS          3

static volatile int32_t  s_encoderNum = 0;
static volatile int32_t  s_encoderDelta = 0;
static volatile bool     s_button_edge = false;
static int      s_prevA;
static bool     s_button_raw;

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

    for (;;) {
        /* ── 按键消抖 ── */
        bool raw = (gpio_get_level(ROTARY_ENC_SW_PIN) == 0);
        if (raw == prevButton) {
            btnStable = 0;
        } else {
            btnStable++;
            if (btnStable >= DEBOUNCE_TICKS) {
                prevButton = raw;
                btnStable = 0;
                s_button_raw = raw;
                s_button_edge = true;
            }
        }

        /* ── 累计 delta ── */
        int32_t n = s_encoderNum;
        if (n != 0) {
            s_encoderNum = 0;
            s_encoderDelta += n;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ─── 公开 API ────────────────────────────────────────────── */

int32_t rotaryEncoder_getDelta(void)
{
    int32_t d = s_encoderDelta;
    s_encoderDelta = 0;
    return d;
}

bool rotaryEncoder_buttonEdge(void)
{
    bool e = s_button_edge;
    s_button_edge = false;
    return e;
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
