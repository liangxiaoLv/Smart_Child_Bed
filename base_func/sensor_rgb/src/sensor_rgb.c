#include "sensor_rgb.h"
#include "ap3216c.h"
#include "rgb_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdlib.h>

/* ── 亮度映射参数 ── */
#define ALS_DARK            0       /* 最暗 → 亮度 100% */
#define ALS_BRIGHT          5000    /* 最亮 → 亮度 10%  */
#define BRIGHTNESS_MIN      10      /* 灯带最小亮度百分比 */
#define BRIGHTNESS_MAX      100     /* 灯带最大亮度百分比 */
#define HYSTERESIS          3       /* 亮度变化阈值（百分比），避免频繁抖动 */

#define POLL_INTERVAL_MS    500

static const char *TAG = "sensor_rgb";
static bool s_enabled = false;
static uint8_t s_last_pct = 255;    /* 非法初始值，保证首次必定写入 */

/* ── ALS raw → 亮度百分比（线性反向映射） ── */
static uint8_t alsToBrightness(uint16_t als)
{
    if (als <= ALS_DARK) return BRIGHTNESS_MAX;
    if (als >= ALS_BRIGHT) return BRIGHTNESS_MIN;

    /* 线性：als=0→100%, als=ALS_BRIGHT→10% */
    int pct = BRIGHTNESS_MAX - (int)(als * (BRIGHTNESS_MAX - BRIGHTNESS_MIN) / ALS_BRIGHT);
    if (pct < BRIGHTNESS_MIN) pct = BRIGHTNESS_MIN;
    if (pct > BRIGHTNESS_MAX) pct = BRIGHTNESS_MAX;
    return (uint8_t)pct;
}

static void sensorCtrlTask(void *arg)
{
    for (;;) {
        if (s_enabled) {
            uint16_t als = ap3216c_getAls();
            uint8_t pct = alsToBrightness(als);

            if (abs((int)pct - (int)s_last_pct) >= HYSTERESIS) {
                rgbLed_setBrightness(pct);
                s_last_pct = pct;
                ESP_LOGI(TAG, "ALS=%u → 亮度 %u%%", als, pct);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t SENSOR_CONTROL_RGB(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = ap3216c_init(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP3216C 初始化失败，自动控制未启动");
        return ret;
    }

    s_enabled = true;
    s_last_pct = 255;

    xTaskCreate(sensorCtrlTask, "sensor_rgb", 2560, NULL, 3, NULL);
    ESP_LOGI(TAG, "环境光→RGB 自动控制已启动");
    return ESP_OK;
}

esp_err_t sensorCtrlRgb_enable(bool en)
{
    s_enabled = en;
    s_last_pct = 255;   /* 重新使能时强制刷新亮度 */
    ESP_LOGI(TAG, "自动控制: %s", en ? "开" : "关");
    return ESP_OK;
}

uint16_t sensorCtrlRgb_getAls(void)
{
    return ap3216c_getAls();
}
