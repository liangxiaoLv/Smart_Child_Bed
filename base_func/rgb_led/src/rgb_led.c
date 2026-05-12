#include "rgb_led.h"
#include "pin_map.h"
#include "ws2812_driver.h"
#include "xl9555_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>

#define DEBOUNCE_MS         30
#define BREATH_PERIOD_MS    3000
#define CYCLE_PERIOD_MS     4000
#define TASK_TICK_MS        20
#define BREATH_MIN           0.05f

static const char *TAG = "rgb_led";

typedef enum {
    RGB_OFF,
    RGB_SOLID,
    RGB_BREATHING,
    RGB_CYCLE,
} rgbState_t;

static rgbState_t s_state = RGB_OFF;

static bool keyEdge(xl9555_port_t port, uint8_t mask)
{
    static int cnt[2];
    static bool db[2];
    int idx = (mask == XL9555_KEY1) ? 0 : 1;

    int thresh = DEBOUNCE_MS / TASK_TICK_MS;
    bool raw = !xl9555Driver_getPin(port, mask);

    if (raw) {
        if (cnt[idx] < 255) cnt[idx]++;
    } else {
        cnt[idx] = 0;
    }
    bool stable = (cnt[idx] >= thresh);
    bool edge = stable && !db[idx];
    db[idx] = stable;
    return edge;
}

static void fillColor(uint8_t r, uint8_t g, uint8_t b)
{
    ws2812Driver_setAll(r, g, b);
    ws2812Driver_flush();
}

static void hsvToRgb(float h, float s, float v, float *r, float *g, float *b)
{
    float c = v * s;
    float hp = h / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    if (hp < 1)      { *r = c; *g = x; *b = 0; }
    else if (hp < 2) { *r = x; *g = c; *b = 0; }
    else if (hp < 3) { *r = 0; *g = c; *b = x; }
    else if (hp < 4) { *r = 0; *g = x; *b = c; }
    else if (hp < 5) { *r = x; *g = 0; *b = c; }
    else             { *r = c; *g = 0; *b = x; }
    float m = v - c;
    *r += m; *g += m; *b += m;
}

static void rgbLedTask(void *arg)
{
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        bool k1 = keyEdge(XL9555_PORT1, XL9555_KEY1);
        bool k2 = keyEdge(XL9555_PORT1, XL9555_KEY2);

        switch (s_state) {
        case RGB_OFF:
            if (k1) {
                s_state = RGB_SOLID;
                ESP_LOGI(TAG, "→ 常亮");
            }
            break;

        case RGB_SOLID:
            if (k1) {
                s_state = RGB_OFF;
                fillColor(0, 0, 0);
                ESP_LOGI(TAG, "→ 熄灭");
            } else if (k2) {
                s_state = RGB_BREATHING;
                ESP_LOGI(TAG, "→ 呼吸灯");
            }
            break;

        case RGB_BREATHING:
            if (k1) {
                s_state = RGB_OFF;
                fillColor(0, 0, 0);
                ESP_LOGI(TAG, "→ 熄灭");
            } else if (k2) {
                s_state = RGB_CYCLE;
                ESP_LOGI(TAG, "→ 色相循环");
            }
            break;

        case RGB_CYCLE:
            if (k1) {
                s_state = RGB_OFF;
                fillColor(0, 0, 0);
                ESP_LOGI(TAG, "→ 熄灭");
            } else if (k2) {
                s_state = RGB_SOLID;
                ESP_LOGI(TAG, "→ 常亮");
            }
            break;
        }

        switch (s_state) {
        case RGB_SOLID:
            fillColor(255, 255, 255);
            break;
        case RGB_BREATHING: {
            uint32_t t = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            float phase = (float)(t % BREATH_PERIOD_MS) / (float)BREATH_PERIOD_MS;
            float sinVal = (sinf(phase * 2.0f * M_PI - M_PI / 2.0f) + 1.0f) / 2.0f;
            float b = BREATH_MIN + (1.0f - BREATH_MIN) * sinVal;
            uint8_t v = (uint8_t)(b * 255.0f);
            fillColor(v, v, v);
            break;
        }
        case RGB_CYCLE: {
            uint32_t t = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            float hue = fmodf((float)t / (float)CYCLE_PERIOD_MS * 360.0f, 360.0f);
            float r, g, b;
            hsvToRgb(hue, 1.0f, 1.0f, &r, &g, &b);
            fillColor((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
            break;
        }
        default:
            break;
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(TASK_TICK_MS));
    }
}

esp_err_t rgbLed_work(void)
{
    esp_err_t ret = xl9555Driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "XL9555 初始化失败");
        return ret;
    }

    ret = ws2812Driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 初始化失败");
        return ret;
    }

    ws2812Driver_off();

    xTaskCreate(rgbLedTask, "rgb_led", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "RGB 灯带任务已启动");
    return ESP_OK;
}
