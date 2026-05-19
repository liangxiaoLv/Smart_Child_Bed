#include "ws2812_driver.h"
#include "led_strip_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ws2812";

struct ws2812_handle {
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t encoder;
    uint8_t *pixels;
    int led_num;
};

ws2812_handle_t ws2812Driver_new(int gpio, int led_num)
{
    struct ws2812_handle *h = calloc(1, sizeof(*h));
    if (!h) {
        ESP_LOGE(TAG, "handle 分配失败");
        return NULL;
    }
    h->led_num = led_num;
    h->pixels = calloc(1, led_num * 3);
    if (!h->pixels) {
        ESP_LOGE(TAG, "像素缓冲区分配失败 (%d bytes)", led_num * 3);
        free(h);
        return NULL;
    }

    /* 上电后首次复位：GPIO 直驱 LOW 300µs，满足 WS2812 >50µs 要求 */
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(300);

    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = gpio,
        .mem_block_symbols = 128,
        .resolution_hz = 10000000,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_cfg, &h->chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT 通道创建失败");
        ws2812Driver_free(h);
        return NULL;
    }

    led_strip_encoder_config_t enc_cfg = { .resolution = 10000000 };
    ret = rmt_new_led_strip_encoder(&enc_cfg, &h->encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "编码器创建失败");
        ws2812Driver_free(h);
        return NULL;
    }

    ret = rmt_enable(h->chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT 使能失败");
        ws2812Driver_free(h);
        return NULL;
    }

    gpio_set_drive_capability(gpio, GPIO_DRIVE_CAP_3);

    ESP_LOGI(TAG, "WS2812 初始化完成, GPIO=%d, LED=%d", gpio, led_num);
    return h;
}

void ws2812Driver_free(ws2812_handle_t h)
{
    if (!h) return;
    if (h->chan) {
        rmt_disable(h->chan);
        rmt_del_channel(h->chan);
    }
    if (h->encoder) rmt_del_encoder(h->encoder);
    free(h->pixels);
    free(h);
}

esp_err_t ws2812Driver_setAll(ws2812_handle_t h, uint8_t r, uint8_t g, uint8_t b)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < h->led_num; i++) {
        h->pixels[i * 3 + 0] = g;
        h->pixels[i * 3 + 1] = r;
        h->pixels[i * 3 + 2] = b;
    }
    return ESP_OK;
}

esp_err_t ws2812Driver_setPixel(ws2812_handle_t h, int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (!h || idx < 0 || idx >= h->led_num) return ESP_ERR_INVALID_ARG;
    h->pixels[idx * 3 + 0] = g;
    h->pixels[idx * 3 + 1] = r;
    h->pixels[idx * 3 + 2] = b;
    return ESP_OK;
}

esp_err_t ws2812Driver_flush(ws2812_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(h->chan, h->encoder, h->pixels,
                                  h->led_num * 3, &tx_cfg);
    if (ret != ESP_OK) return ret;
    return rmt_tx_wait_all_done(h->chan, portMAX_DELAY);
}

esp_err_t ws2812Driver_off(ws2812_handle_t h)
{
    if (!h) return ESP_ERR_INVALID_ARG;
    ws2812Driver_setAll(h, 0, 0, 0);
    return ws2812Driver_flush(h);
}
