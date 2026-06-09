/*
 * RGB 16×16×4 大屏驱动
 * - 4 块 16×16 WS2812 横向拼接, 共 1024 LED
 * - 蛇形走线 (zig-zag), 按列扫描, 奇数列反序
 * - 字符资源来自 4 个 JSON, 编译期转成 C 数组内嵌
 * - 启动后显示第一组 (ABCD), 调 rgbScreen16x16x4_next() 翻页
 */

#include "rgb_screen_16_16_4.h"
#include "pin_map.h"
#include "ws2812_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"

#include "art_data.h"   /* 编译期生成的 C 数组 */

static const char *TAG = "rgb164";
static ws2812_handle_t s_ws = NULL;

/* ─── 全局状态 ────────────────────────────────────────────── */
static uint8_t s_page = 0;          /* 当前页码 */
static uint8_t s_brightness = 255;  /* 0~255, 默认全亮 */

/* 全局亮度缩放 (应用到 r/g/b) */
static inline void applyBrightness(uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s_brightness == 255) return;
    *r = (uint8_t)(*r * s_brightness / 255);
    *g = (uint8_t)(*g * s_brightness / 255);
    *b = (uint8_t)(*b * s_brightness / 255);
}

/* ─── 地址映射 ──────────────────────────────────────────────
 * 实际屏幕为单方向扫描, 但列方向是底到顶 (需要上下翻转)
 * block = x / 16, col_in = x % 16, row_in = 15 - y
 */
static inline int pixelAddr(uint8_t x, uint8_t y)
{
    uint8_t block  = x >> 4;          /* x / 16 */
    uint8_t col_in = x & 0x0F;        /* x % 16 */
    return (block << 8) | (col_in << 4) | (15 - y);
}

/* ─── API ──────────────────────────────────────────────────── */
esp_err_t rgbScreen16x16x4_init(void)
{
    s_ws = ws2812Driver_new(RGB_SCREEN_16_16_4_DATA_PIN,
                            RGB_SCREEN_16_16_4_LED_NUM);
    if (!s_ws) {
        ESP_LOGE(TAG, "WS2812 初始化失败");
        return ESP_FAIL;
    }
    ws2812Driver_off(s_ws);
    ESP_LOGI(TAG, "16x16x4 点阵屏初始化完成");
    return rgbScreen16x16x4_reset();
}

esp_err_t rgbScreen16x16x4_clear(void)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    ESP_ERROR_CHECK(ws2812Driver_setAll(s_ws, 0, 0, 0));
    /* 立即把全 0 推到 LED, 避免翻页时与新图叠加 (残影) */
    return ws2812Driver_flush(s_ws);
}

esp_err_t rgbScreen16x16x4_flush(void)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    return ws2812Driver_flush(s_ws);
}

esp_err_t rgbScreen16x16x4_setPixel(uint8_t x, uint8_t y,
                                    uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    if (x >= SCREEN_W || y >= SCREEN_H) return ESP_ERR_INVALID_ARG;
    applyBrightness(&r, &g, &b);
    return ws2812Driver_setPixel(s_ws, pixelAddr(x, y), r, g, b);
}

esp_err_t rgbScreen16x16x4_drawBlock(uint8_t block_idx,
                                    const uint8_t *matrix,
                                    uint8_t mat_w, uint8_t mat_h,
                                    uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ws) return ESP_ERR_INVALID_STATE;
    if (block_idx >= BLOCK_NUM || !matrix) return ESP_ERR_INVALID_ARG;
    if (mat_w > BLOCK_W || mat_h > BLOCK_H) return ESP_ERR_INVALID_ARG;

    /* 在块内居中 */
    uint8_t ox = (BLOCK_W - mat_w) >> 1;
    uint8_t oy = (BLOCK_H - mat_h) >> 1;
    uint8_t bx0 = block_idx * BLOCK_W;

    for (uint8_t row = 0; row < mat_h; row++) {
        for (uint8_t col = 0; col < mat_w; col++) {
            if (matrix[row * mat_w + col]) {
                uint8_t px = bx0 + ox + col;
                uint8_t py = oy + row;
                uint8_t cr = r, cg = g, cb = b;
                applyBrightness(&cr, &cg, &cb);
                esp_err_t err = ws2812Driver_setPixel(s_ws,
                    pixelAddr(px, py), cr, cg, cb);
                if (err != ESP_OK) return err;
            }
        }
    }
    return ESP_OK;
}

esp_err_t rgbScreen16x16x4_draw4Blocks(const uint8_t *m0, uint8_t w0, uint8_t h0,
                                      const uint8_t *m1, uint8_t w1, uint8_t h1,
                                      const uint8_t *m2, uint8_t w2, uint8_t h2,
                                      const uint8_t *m3, uint8_t w3, uint8_t h3,
                                      uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t err;
    if ((err = rgbScreen16x16x4_clear()) != ESP_OK) return err;
    if ((err = rgbScreen16x16x4_drawBlock(0, m0, w0, h0, r, g, b)) != ESP_OK) return err;
    if ((err = rgbScreen16x16x4_drawBlock(1, m1, w1, h1, r, g, b)) != ESP_OK) return err;
    if ((err = rgbScreen16x16x4_drawBlock(2, m2, w2, h2, r, g, b)) != ESP_OK) return err;
    if ((err = rgbScreen16x16x4_drawBlock(3, m3, w3, h3, r, g, b)) != ESP_OK) return err;
    return rgbScreen16x16x4_flush();
}

/* ─── 翻页逻辑 ────────────────────────────────────────────── */
static const art_entry_t *pageEntry(uint8_t page, uint8_t i)
{
    uint16_t idx = (uint16_t)page * BLOCK_NUM + i;
    if (idx >= ART_TOTAL) return NULL;
    return &ART_TABLE[idx];
}

static esp_err_t renderCurrentPage(void)
{
    /* 先全屏清零, 避免上一页的残影 */
    rgbScreen16x16x4_clear();

    const art_entry_t *e0 = pageEntry(s_page, 0);
    const art_entry_t *e1 = pageEntry(s_page, 1);
    const art_entry_t *e2 = pageEntry(s_page, 2);
    const art_entry_t *e3 = pageEntry(s_page, 3);
    if (!e0 || !e1 || !e2 || !e3) return ESP_ERR_INVALID_STATE;

    /* 每个图标用各自的颜色 (若指定), 否则统一用调用方颜色 */
    uint8_t r = 255, g = 255, b = 255;
    if (e0->r || e0->g || e0->b) { r = e0->r; g = e0->g; b = e0->b; }
    esp_err_t err = rgbScreen16x16x4_drawBlock(0, e0->data, e0->w, e0->h, r, g, b);
    if (err != ESP_OK) return err;

    r = 255; g = 255; b = 255;
    if (e1->r || e1->g || e1->b) { r = e1->r; g = e1->g; b = e1->b; }
    err = rgbScreen16x16x4_drawBlock(1, e1->data, e1->w, e1->h, r, g, b);
    if (err != ESP_OK) return err;

    r = 255; g = 255; b = 255;
    if (e2->r || e2->g || e2->b) { r = e2->r; g = e2->g; b = e2->b; }
    err = rgbScreen16x16x4_drawBlock(2, e2->data, e2->w, e2->h, r, g, b);
    if (err != ESP_OK) return err;

    r = 255; g = 255; b = 255;
    if (e3->r || e3->g || e3->b) { r = e3->r; g = e3->g; b = e3->b; }
    err = rgbScreen16x16x4_drawBlock(3, e3->data, e3->w, e3->h, r, g, b);
    if (err != ESP_OK) return err;

    return rgbScreen16x16x4_flush();
}

esp_err_t rgbScreen16x16x4_next(void)
{
    uint8_t max_page = (ART_TOTAL + BLOCK_NUM - 1) / BLOCK_NUM;
    s_page = (s_page + 1) % max_page;
    ESP_LOGI(TAG, "翻页: %d/%d", s_page + 1, max_page);
    return renderCurrentPage();
}

esp_err_t rgbScreen16x16x4_reset(void)
{
    s_page = 0;
    ESP_LOGI(TAG, "回到首页");
    return renderCurrentPage();
}

/* ─── 亮度调节 ────────────────────────────────────────────── */
#define BRIGHTNESS_STEP   25    /* 10% × 255 ≈ 25 */

esp_err_t rgbScreen16x16x4_brightnessUp(void)
{
    if (s_brightness > 255 - BRIGHTNESS_STEP) s_brightness = 255;
    else s_brightness += BRIGHTNESS_STEP;
    ESP_LOGI(TAG, "亮度 ↑ %d/255 (%d%%)", s_brightness, s_brightness * 100 / 255);
    return renderCurrentPage();
}

esp_err_t rgbScreen16x16x4_brightnessDown(void)
{
    if (s_brightness < BRIGHTNESS_STEP) s_brightness = 0;
    else s_brightness -= BRIGHTNESS_STEP;
    ESP_LOGI(TAG, "亮度 ↓ %d/255 (%d%%)", s_brightness, s_brightness * 100 / 255);
    return renderCurrentPage();
}

/* ─── 按键中断 (下降沿触发) ───────────────────────────────── */
#define BTN_DEBOUNCE_MS  200

static TaskHandle_t s_btn_task = NULL;

/* ISR 上下文不可做复杂操作, 仅发事件给任务 */
typedef struct {
    uint32_t gpio;
    TickType_t tick;
} btn_evt_t;

static QueueHandle_t s_btn_evt = NULL;

static void IRAM_ATTR btn_isr(void *arg)
{
    uint32_t gpio = (uint32_t)(uintptr_t)arg;
    btn_evt_t evt = { .gpio = gpio, .tick = xTaskGetTickCountFromISR() };
    xQueueSendFromISR(s_btn_evt, &evt, NULL);
}

static void btnTask(void *arg)
{
    btn_evt_t evt;
    TickType_t last_key_tick[3] = {0, 0, 0};
    for (;;) {
        if (xQueueReceive(s_btn_evt, &evt, portMAX_DELAY) == pdTRUE) {
            uint8_t idx = 0xFF;
            if (evt.gpio == RGB_SCREEN_KEY0_PIN) idx = 0;
            else if (evt.gpio == RGB_SCREEN_KEY1_PIN) idx = 1;
            else if (evt.gpio == RGB_SCREEN_KEY2_PIN) idx = 2;
            if (idx == 0xFF) continue;

            /* 200ms 内同键重复触发忽略 */
            if ((evt.tick - last_key_tick[idx]) < pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
                continue;
            }
            last_key_tick[idx] = evt.tick;

            switch (idx) {
                case 0: rgbScreen16x16x4_next();             break;  /* 绿 - 翻页 */
                case 1: rgbScreen16x16x4_brightnessUp();     break;  /* 黄 - 变亮 */
                case 2: rgbScreen16x16x4_brightnessDown();   break;  /* 红 - 变暗 */
            }
        }
    }
}

esp_err_t rgbScreen16x16x4_initButtons(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << RGB_SCREEN_KEY0_PIN) |
                        (1ULL << RGB_SCREEN_KEY1_PIN) |
                        (1ULL << RGB_SCREEN_KEY2_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(RGB_SCREEN_KEY0_PIN, btn_isr, (void *)(uintptr_t)RGB_SCREEN_KEY0_PIN));
    ESP_ERROR_CHECK(gpio_isr_handler_add(RGB_SCREEN_KEY1_PIN, btn_isr, (void *)(uintptr_t)RGB_SCREEN_KEY1_PIN));
    ESP_ERROR_CHECK(gpio_isr_handler_add(RGB_SCREEN_KEY2_PIN, btn_isr, (void *)(uintptr_t)RGB_SCREEN_KEY2_PIN));

    s_btn_evt = xQueueCreate(8, sizeof(btn_evt_t));
    if (!s_btn_evt) return ESP_ERR_NO_MEM;
    xTaskCreate(btnTask, "rgb164_btns", 2048, NULL, 10, &s_btn_task);
    ESP_LOGI(TAG, "按键已注册: KEY0(GPIO%d)=翻页 KEY1(%d)=亮 KEY2(%d)=暗",
             RGB_SCREEN_KEY0_PIN, RGB_SCREEN_KEY1_PIN, RGB_SCREEN_KEY2_PIN);
    return ESP_OK;
}
