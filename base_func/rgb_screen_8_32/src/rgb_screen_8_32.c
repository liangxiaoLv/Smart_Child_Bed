#include "rgb_screen_8_32.h"
#include "pin_map.h"
#include "ws2812_driver.h"
#include "esp_log.h"

static const char *TAG = "rgb_scr";
static ws2812_handle_t s_ws;

/* 全局亮度比例: 10% (26/255 ≈ 0.102) */
#define RGB_SCREEN_BRIGHTNESS 5

/* ─── 坐标映射 ────────────────────────────────────────────── */

/*
 * 蛇形走线，LED 0 在 (row 0, col 0)：
 *   偶数列从上到下 (row 0→7)
 *   奇数列从下到上 (row 7→0)
 *   index = col * 8 + (col & 1 ? 7 - row : row)
 */
static inline int pixelIdx(uint8_t row, uint8_t col)
{
    if (col & 1) return col * 8 + (7 - row);
    return col * 8 + row;
}

/* ─── 3×7 数字字模 ────────────────────────────────────────── */

/* 每个字节 3 位：bit2=左, bit1=中, bit0=右，共 7 行 */
static const uint8_t s_digits[10][7] = {
    {0b111, 0b101, 0b101, 0b101, 0b101, 0b101, 0b111}, // 0
    {0b001, 0b001, 0b001, 0b001, 0b001, 0b001, 0b001}, // 1
    {0b111, 0b001, 0b001, 0b111, 0b100, 0b100, 0b111}, // 2
    {0b111, 0b001, 0b001, 0b111, 0b001, 0b001, 0b111}, // 3
    {0b101, 0b101, 0b101, 0b111, 0b001, 0b001, 0b001}, // 4
    {0b111, 0b100, 0b100, 0b111, 0b001, 0b001, 0b111}, // 5
    {0b111, 0b100, 0b100, 0b111, 0b101, 0b101, 0b111}, // 6
    {0b111, 0b001, 0b001, 0b001, 0b001, 0b001, 0b001}, // 7
    {0b111, 0b101, 0b101, 0b111, 0b101, 0b101, 0b111}, // 8
    {0b111, 0b101, 0b101, 0b111, 0b001, 0b001, 0b111}, // 9
};

/* ─── 公共 API ────────────────────────────────────────────── */

esp_err_t rgbScreen_init(void)
{
    s_ws = ws2812Driver_new(RGB_SCREEN_DATA_PIN, RGB_SCREEN_LED_NUM);
    if (!s_ws) {
        ESP_LOGE(TAG, "点阵屏 WS2812 初始化失败");
        return ESP_FAIL;
    }
    ws2812Driver_off(s_ws);
    ESP_LOGI(TAG, "点阵屏初始化完成");
    return ESP_OK;
}

esp_err_t rgbScreen_setAll(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    ws2812Driver_setAll(s_ws, r, g, b);
    return ws2812Driver_flush(s_ws);
}

esp_err_t rgbScreen_setPixel(uint8_t row, uint8_t col,
                              uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    if (row > 7 || col > 31) return ESP_ERR_INVALID_ARG;
    ws2812Driver_setPixel(s_ws, pixelIdx(row, col),
        (uint8_t)(r * RGB_SCREEN_BRIGHTNESS / 255),
        (uint8_t)(g * RGB_SCREEN_BRIGHTNESS / 255),
        (uint8_t)(b * RGB_SCREEN_BRIGHTNESS / 255));
    return ESP_OK;
}

esp_err_t rgbScreen_drawDigit(uint8_t col, uint8_t digit,
                               uint8_t r, uint8_t g, uint8_t b)
{
    if (digit > 9 || col > 29) return ESP_ERR_INVALID_ARG;

    for (int row = 0; row < 7; row++) {
        uint8_t bits = s_digits[digit][row];
        if (bits & 0b100) rgbScreen_setPixel(row + 1, col,     r, g, b);
        if (bits & 0b010) rgbScreen_setPixel(row + 1, col + 1, r, g, b);
        if (bits & 0b001) rgbScreen_setPixel(row + 1, col + 2, r, g, b);
    }
    return ESP_OK;
}

esp_err_t rgbScreen_drawColon(bool on, uint8_t r, uint8_t g, uint8_t b)
{
    if (on) {
        rgbScreen_setPixel(2, 22, r, g, b);
        rgbScreen_setPixel(4, 22, r, g, b);
    }
    return ESP_OK;
}

esp_err_t rgbScreen_clear(void)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    return ws2812Driver_setAll(s_ws, 0, 0, 0);
}

esp_err_t rgbScreen_flush(void)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    return ws2812Driver_flush(s_ws);
}

/* ─── 完整帧渲染 ──────────────────────────────────────────── */

esp_err_t rgbScreen_renderFrame(float temperature, uint8_t aqi,
                                 int hour, int minute, int second)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;

    rgbScreen_clear();

    /* Row 0 分隔/装饰点 */
    rgbScreen_setPixel(0,  0, 0, 0, 30);
    rgbScreen_setPixel(0,  1, 0, 0, 30);
    rgbScreen_setPixel(0,  5, 0, 0, 30);
    rgbScreen_setPixel(0, 13, 0, 0, 30);
    rgbScreen_setPixel(0, 14, 0, 0, 30);
    rgbScreen_setPixel(0, 30, 0, 0, 30);
    rgbScreen_setPixel(0, 31, 0, 0, 30);

    /* 空气质量 1 位 (col 2-4) */
    uint8_t ar = 0, ag = 255, ab = 0;
    if      (aqi <= 2) { ar = 0;   ag = 255; ab = 0;   }
    else if (aqi <= 3) { ar = 255; ag = 255; ab = 0;   }
    else if (aqi <= 4) { ar = 255; ag = 128; ab = 0;   }
    else               { ar = 255; ag = 0;   ab = 0;   }
    rgbScreen_drawDigit(2, aqi > 9 ? 9 : aqi, ar, ag, ab);

    /* 温度 2 位 (col 6-8 十位, col 10-12 个位) */
    uint8_t tr, tg, tb;
    int t = (int)temperature;
    if      (t < 10)  { tr = 0;   tg = 100; tb = 255; }
    else if (t <= 30) { tr = 200; tg = 255; tb = 200; }
    else              { tr = 255; tg = 128; tb = 0;   }
    rgbScreen_drawDigit( 6, (t / 10) % 10, tr, tg, tb);
    rgbScreen_drawDigit(10, t % 10,        tr, tg, tb);

    /* 时间 4 位 */
    uint8_t wr = 200, wg = 200, wb = 255;
    rgbScreen_drawDigit(15, hour   / 10, wr, wg, wb);
    rgbScreen_drawDigit(19, hour   % 10, wr, wg, wb);
    rgbScreen_drawDigit(23, minute / 10, wr, wg, wb);
    rgbScreen_drawDigit(27, minute % 10, wr, wg, wb);

    /* 冒号 */
    rgbScreen_drawColon((second & 1) == 0, 255, 255, 255);

    return rgbScreen_flush();
}
