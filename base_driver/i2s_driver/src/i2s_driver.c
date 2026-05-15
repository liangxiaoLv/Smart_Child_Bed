#include "i2s_driver.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "i2s_driver";

static i2s_chan_handle_t s_tx_chan;
static bool s_inited;

esp_err_t i2sDriver_init(const i2sDriver_config_t *config)
{
    if (s_inited) {
        ESP_LOGW(TAG, "already inited");
        return ESP_OK;
    }
    if (!config) return ESP_ERR_INVALID_ARG;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(config->sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        config->bits_per_sample,
                        config->stereo ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = config->mclk_io,
            .bclk = config->bclk_io,
            .ws   = config->ws_io,
            .dout = config->dout_io,
            .din  = config->din_io,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    ret = i2s_channel_enable(s_tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return ret;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2S ready: %luHz %dbit %s (bclk=%d ws=%d dout=%d)",
             config->sample_rate, config->bits_per_sample,
             config->stereo ? "stereo" : "mono",
             config->bclk_io, config->ws_io, config->dout_io);
    return ESP_OK;
}

esp_err_t i2sDriver_write(const uint8_t *data, size_t bytes, size_t *written, uint32_t timeout_ms)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    return i2s_channel_write(s_tx_chan, data, bytes, written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t i2sDriver_read(uint8_t *buf, size_t bytes, size_t *read, uint32_t timeout_ms)
{
    if (!s_tx_chan) return ESP_ERR_INVALID_STATE;
    return i2s_channel_read(s_tx_chan, buf, bytes, read, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t i2sDriver_deinit(void)
{
    if (!s_tx_chan) return ESP_OK;
    i2s_channel_disable(s_tx_chan);
    i2s_del_channel(s_tx_chan);
    s_tx_chan = NULL;
    s_inited = false;
    ESP_LOGI(TAG, "I2S deinit");
    return ESP_OK;
}

bool i2sDriver_isInited(void)
{
    return s_inited;
}
