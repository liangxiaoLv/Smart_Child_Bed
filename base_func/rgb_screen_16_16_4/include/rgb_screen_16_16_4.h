#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* 屏幕规格：4 块 16×16 横向拼接 = 64×16 像素，1024 LED */
#define SCREEN_W   64
#define SCREEN_H   16
#define BLOCK_W    16
#define BLOCK_H    16
#define BLOCK_NUM  4
#define LED_NUM    1024

/* 一页显示 4 个字符/图标，分别在 4 块 16×16 块上 */

/* 初始化点阵屏硬件 */
esp_err_t rgbScreen16x16x4_init(void);

/* 清屏 */
esp_err_t rgbScreen16x16x4_clear(void);

/* 刷新硬件 (双缓冲) */
esp_err_t rgbScreen16x16x4_flush(void);

/* 单像素 API (x: 0~63, y: 0~15) */
esp_err_t rgbScreen16x16x4_setPixel(uint8_t x, uint8_t y,
                                    uint8_t r, uint8_t g, uint8_t b);

/* 在指定 16×16 块 (0~3) 上居中绘制 dot_matrix (任意尺寸) */
esp_err_t rgbScreen16x16x4_drawBlock(uint8_t block_idx,
                                    const uint8_t *matrix,
                                    uint8_t mat_w, uint8_t mat_h,
                                    uint8_t r, uint8_t g, uint8_t b);

/* 全屏一次性渲染 4 个 dot_matrix (每块一个) */
esp_err_t rgbScreen16x16x4_draw4Blocks(const uint8_t *m0, uint8_t w0, uint8_t h0,
                                      const uint8_t *m1, uint8_t w1, uint8_t h1,
                                      const uint8_t *m2, uint8_t w2, uint8_t h2,
                                      const uint8_t *m3, uint8_t w3, uint8_t h3,
                                      uint8_t r, uint8_t g, uint8_t b);

/* 翻页：显示下一页的 4 个设计 (按 key1 触发) */
esp_err_t rgbScreen16x16x4_next(void);

/* 回到首页 */
esp_err_t rgbScreen16x16x4_reset(void);

/* KEY0: 亮度 +10% (0~255 范围) */
esp_err_t rgbScreen16x16x4_brightnessUp(void);

/* KEY3: 亮度 -10% (0~255 范围) */
esp_err_t rgbScreen16x16x4_brightnessDown(void);

/* 初始化按键中断 (KEY0=绿=翻页, KEY1=黄=亮, KEY2=红=暗) */
esp_err_t rgbScreen16x16x4_initButtons(void);
