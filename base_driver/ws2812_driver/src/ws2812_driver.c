#include "ws2812_driver.h"
#include "pin_map.h"
#include "led_strip_encoder.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ws2812";

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_encoder;
static uint8_t s_pixels[WS2812_LED_NUM * 3];

esp_err_t ws2812Driver_init(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = WS2812_DATA_PIN,
        .mem_block_symbols = 64,
        .resolution_hz = WS2812_RMT_RES_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t ret = rmt_new_tx_channel(&tx_cfg, &s_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT 通道创建失败");
        return ret;
    }

    led_strip_encoder_config_t enc_cfg = {
        .resolution = WS2812_RMT_RES_HZ,
    };
    ret = rmt_new_led_strip_encoder(&enc_cfg, &s_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "编码器创建失败");
        return ret;
    }

    ret = rmt_enable(s_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RMT 使能失败");
        return ret;
    }

    ESP_LOGI(TAG, "WS2812 初始化完成, LED=%d", WS2812_LED_NUM);
    return ESP_OK;
}

esp_err_t ws2812Driver_setAll(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < WS2812_LED_NUM; i++) {
        s_pixels[i * 3 + 0] = g;
        s_pixels[i * 3 + 1] = r;
        s_pixels[i * 3 + 2] = b;
    }
    return ESP_OK;
}

esp_err_t ws2812Driver_flush(void)
{
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(s_chan, s_encoder, s_pixels, sizeof(s_pixels), &tx_cfg);
    if (ret != ESP_OK) return ret;
    return rmt_tx_wait_all_done(s_chan, portMAX_DELAY);
}

esp_err_t ws2812Driver_off(void)
{
    ws2812Driver_setAll(0, 0, 0);
    return ws2812Driver_flush();
}
