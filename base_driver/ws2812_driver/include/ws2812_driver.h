#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t ws2812Driver_init(void);
esp_err_t ws2812Driver_setAll(uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812Driver_flush(void);
esp_err_t ws2812Driver_off(void);
