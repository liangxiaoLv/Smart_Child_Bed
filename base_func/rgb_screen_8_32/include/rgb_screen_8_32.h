#pragma once

#include "esp_err.h"
#include <stdint.h>

/* 启动屏幕，常亮显示 */
esp_err_t rgbScreen_init(void);

/* 设置所有 LED 颜色 (0-255) */
esp_err_t rgbScreen_setAll(uint8_t r, uint8_t g, uint8_t b);
