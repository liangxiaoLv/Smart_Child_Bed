#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct ws2812_handle * ws2812_handle_t;

/* 创建 / 销毁实例 */
ws2812_handle_t ws2812Driver_new(int gpio, int led_num);
void ws2812Driver_free(ws2812_handle_t h);

/* 操作 */
esp_err_t ws2812Driver_setAll(ws2812_handle_t h, uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812Driver_setPixel(ws2812_handle_t h, int idx, uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812Driver_flush(ws2812_handle_t h);
esp_err_t ws2812Driver_off(ws2812_handle_t h);
