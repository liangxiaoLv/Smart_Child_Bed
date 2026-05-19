#include "rgb_screen_8_32.h"
#include "pin_map.h"
#include "ws2812_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "rgb_scr";
static ws2812_handle_t s_ws = NULL;

esp_err_t rgbScreen_init(void)
{
    s_ws = ws2812Driver_new(RGB_SCREEN_DATA_PIN, RGB_SCREEN_LED_NUM);
    if (!s_ws) {
        ESP_LOGE(TAG, "点阵屏 WS2812 初始化失败");
        return ESP_FAIL;
    }

    ws2812Driver_off(s_ws);
    vTaskDelay(pdMS_TO_TICKS(50));

    rgbScreen_setAll(10, 16, 13);
    return ESP_OK;
}

esp_err_t rgbScreen_setAll(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    ws2812Driver_setAll(s_ws, r, g, b);
    // ws2812Driver_setPixel(s_ws, 0, 0, 0, 0);
    return ws2812Driver_flush(s_ws);
}
