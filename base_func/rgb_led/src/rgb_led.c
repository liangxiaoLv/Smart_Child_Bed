/**
 * RGB 灯带 + 旋转编码器控制（WS2812, IO13）
 * ==========================================
 * 旋转编码器按键 → 开/关
 * 旋转编码器旋钮 → 亮度 +/-5（范围 0~255）
 * 上电默认关。打开时初始亮度 128。
 */
#include "rgb_led.h"
#include "pin_map.h"
#include "ws2812_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "rgb_led";

/* ─── 旋转编码器：硬件参数 ───────────────────────────────── */
#define ENC_POLL_US        500
#define BTN_DEBOUNCE_TICKS   3   /* × task tick(20ms) = 60ms */

/* ─── 灯带参数 ───────────────────────────────────────────── */
#define TASK_TICK_MS        20
#define BRIGHTNESS_STEP      5
#define BRIGHTNESS_DEFAULT 128
#define BRIGHTNESS_MAX     255

/* ─── 全局状态 ───────────────────────────────────────────── */
static ws2812_handle_t s_ws = NULL;
static bool      s_on         = false;
static int       s_brightness = BRIGHTNESS_DEFAULT;

/* 旋转编码器：A/B 相由定时器中断更新 */
static volatile int32_t s_enc_raw = 0;
static int      s_prev_a;

/* ─── 定时器回调：A/B 相轮询（500us 高精度） ─────────────── */
static void encPollCB(void *arg)
{
    (void)arg;
    int a = gpio_get_level(ROTARY_ENC_A_PIN);
    if (a != s_prev_a) {
        int b = gpio_get_level(ROTARY_ENC_B_PIN);
        if (b != s_prev_a) s_enc_raw++;
        else                s_enc_raw--;
    }
    s_prev_a = a;
}

/* ─── 输出 ──────────────────────────────────────────────── */
static void showColor(void)
{
    if (!s_ws) return;
    if (s_on) {
        uint8_t v = (uint8_t)s_brightness;
        ws2812Driver_setAll(s_ws, v, v, v);
        ws2812Driver_flush(s_ws);
    } else {
        ws2812Driver_off(s_ws);
    }
}

/* ─── 主控任务：按键消抖 + 旋钮读数 + 灯带刷新 ──────────── */
static void rgbLedTask(void *arg)
{
    (void)arg;
    TickType_t lastWake = xTaskGetTickCount();

    int32_t  enc_last  = 0;
    int      btn_cnt   = 0;
    bool     btn_stable = true;      /* 上拉，未按下 = true */
    bool     btn_prev   = true;

    /* 上电默认关 */
    showColor();

    for (;;) {
        /* ── 按键消抖 ── */
        bool raw = (gpio_get_level(ROTARY_ENC_SW_PIN) == 0); /* 按下=低 */
        if (raw == btn_prev) {
            btn_cnt = 0;
        } else {
            if (++btn_cnt >= BTN_DEBOUNCE_TICKS) {
                btn_prev   = raw;
                btn_stable = raw;
                btn_cnt    = 0;

                /* 边沿：按下触发切换 */
                if (btn_stable) {
                    s_on = !s_on;
                    if (s_on) {
                        s_brightness = BRIGHTNESS_DEFAULT;
                    }
                    ESP_LOGI(TAG, "%s (亮度=%d)", s_on ? "开灯" : "关灯", s_brightness);
                    showColor();
                }
            }
        }

        /* ── 旋钮读数 ── */
        int32_t cur = s_enc_raw;
        int32_t delta = cur - enc_last;
        enc_last = cur;
        if (delta != 0) {
            /* 每 2 个原始脉冲 = 1 个刻度 */
            int steps = (int)(delta / 2);
            if (steps != 0) {
                s_brightness += steps * BRIGHTNESS_STEP;
                if (s_brightness < 0)             s_brightness = 0;
                if (s_brightness > BRIGHTNESS_MAX) s_brightness = BRIGHTNESS_MAX;
                ESP_LOGI(TAG, "亮度=%d", s_brightness);
                if (s_on) showColor();
            }
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TASK_TICK_MS));
    }
}

/* ─── 公开 API ────────────────────────────────────────────── */

esp_err_t rgbLed_init(void)
{
    /* 1. 旋转编码器 GPIO */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << ROTARY_ENC_A_PIN) |
                        (1ULL << ROTARY_ENC_B_PIN) |
                        (1ULL << ROTARY_ENC_SW_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_prev_a = gpio_get_level(ROTARY_ENC_A_PIN);

    /* 2. 启动 500us 定时器轮询 A/B 相 */
    const esp_timer_create_args_t tmr = {
        .callback = encPollCB,
        .name     = "enc_poll",
    };
    esp_timer_handle_t timer;
    esp_timer_create(&tmr, &timer);
    esp_timer_start_periodic(timer, ENC_POLL_US);

    /* 3. WS2812 */
    s_ws = ws2812Driver_new(RGB_LED_DATA_PIN, RGB_LED_NUM);
    if (!s_ws) {
        ESP_LOGE(TAG, "WS2812 初始化失败");
        return ESP_FAIL;
    }
    ws2812Driver_off(s_ws);

    /* 4. 主控任务 */
    xTaskCreate(rgbLedTask, "rgb_led", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "RGB 灯带已启动 (IO%d, %d LEDs)", RGB_LED_DATA_PIN, RGB_LED_NUM);
    return ESP_OK;
}

esp_err_t rgbLed_setOnOff(bool on)
{
    s_on = on;
    if (s_on && s_brightness == 0) {
        s_brightness = BRIGHTNESS_DEFAULT;
    }
    showColor();
    return ESP_OK;
}

esp_err_t rgbLed_setBrightness(uint8_t val)
{
    s_brightness = (int)val;
    if (s_brightness > BRIGHTNESS_MAX) s_brightness = BRIGHTNESS_MAX;
    if (s_on) showColor();
    return ESP_OK;
}

esp_err_t rgbLed_setMode(const char *mode)
{
    (void)mode;
    return rgbLed_setOnOff(true);
}
