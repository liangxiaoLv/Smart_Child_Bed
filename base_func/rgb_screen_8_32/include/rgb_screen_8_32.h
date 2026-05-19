#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* 初始化点阵屏硬件，启动后黑屏等待渲染 */
esp_err_t rgbScreen_init(void);

/* 设置所有 LED 颜色 (0-255) */
esp_err_t rgbScreen_setAll(uint8_t r, uint8_t g, uint8_t b);

/* 设置单个像素 (row 0-7, col 0-31)，内部自动映射蛇形走线索引 */
esp_err_t rgbScreen_setPixel(uint8_t row, uint8_t col,
                              uint8_t r, uint8_t g, uint8_t b);

/* 在 col 起始列绘制 3×7 数字 (0-9)，占用物理行 1-7 */
esp_err_t rgbScreen_drawDigit(uint8_t col, uint8_t digit,
                               uint8_t r, uint8_t g, uint8_t b);

/* 绘制/清除冒号 (列22, 行2和行4)，on=true 点亮 */
esp_err_t rgbScreen_drawColon(bool on, uint8_t r, uint8_t g, uint8_t b);

/* 清屏（全部置零，不刷新硬件） */
esp_err_t rgbScreen_clear(void);

/* 将缓冲区刷新到硬件 */
esp_err_t rgbScreen_flush(void);

/* 渲染完整一帧：分隔线 + AQI数字 + 温度数字 + 时间 + 冒号，并刷新硬件 */
esp_err_t rgbScreen_renderFrame(float temperature, uint8_t aqi,
                                 int hour, int minute, int second);
