#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief 初始化 WS2812 RGB 灯带（IO13）并启动控制任务 */
esp_err_t rgbLed_init(void);

/** @brief 外部控制：开/关 */
esp_err_t rgbLed_setOnOff(bool on);

/** @brief 外部控制：设置亮度 0~255 */
esp_err_t rgbLed_setBrightness(uint8_t val);

/** @brief 兼容旧接口：开灯（忽略模式名） */
esp_err_t rgbLed_setMode(const char *mode);
