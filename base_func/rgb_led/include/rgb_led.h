#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t rgbLed_work(void);

/* 外部控制接口（网页/云端调用） */
esp_err_t rgbLed_setOnOff(bool on);
esp_err_t rgbLed_setMode(const char *mode);   /* "solid" / "breath" / "rainbow" */
esp_err_t rgbLed_setBrightness(uint8_t pct);  /* 10 ~ 100 */
