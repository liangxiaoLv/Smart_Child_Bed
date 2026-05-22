#include "i2s_driver.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *TAG = "i2s_driver";

#define I2S_MAX_PORT 2
static i2sDriver_handle_t s_handle[I2S_MAX_PORT];

struct i2sDriver_t {
    int port;
    i2s_chan_handle_t tx_chan;
    i2s_chan_handle_t rx_chan;
};

esp_err_t i2sDriver_init(int port, const i2sDriver_config_t *config, i2sDriver_handle_t *handle_out)
{
    if (port < 0 || port >= I2S_MAX_PORT) {
        ESP_LOGE(TAG, "I2S 端口号 %d 无效", port);
        return ESP_ERR_INVALID_ARG;
    }
    if (!config || !handle_out) return ESP_ERR_INVALID_ARG;
    if (!config->enable_tx && !config->enable_rx) return ESP_ERR_INVALID_ARG;

    if (s_handle[port]) {
        ESP_LOGI(TAG, "I2S%d 通道已存在，复用句柄", port);
        *handle_out = s_handle[port];
        return ESP_OK;
    }

    i2sDriver_handle_t handle = calloc(1, sizeof(*handle));
    if (!handle) return ESP_ERR_NO_MEM;
    handle->port = port;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    i2s_chan_handle_t *p_tx = config->enable_tx ? &handle->tx_chan : NULL;
    i2s_chan_handle_t *p_rx = config->enable_rx ? &handle->rx_chan : NULL;

    esp_err_t ret = i2s_new_channel(&chan_cfg, p_tx, p_rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        free(handle);
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

    if (handle->tx_chan) {
        ret = i2s_channel_init_std_mode(handle->tx_chan, &std_cfg);
        if (ret != ESP_OK) goto fail;
        ret = i2s_channel_enable(handle->tx_chan);
        if (ret != ESP_OK) goto fail;
    }

    if (handle->rx_chan) {
        ret = i2s_channel_init_std_mode(handle->rx_chan, &std_cfg);
        if (ret != ESP_OK) goto fail;
        ret = i2s_channel_enable(handle->rx_chan);
        if (ret != ESP_OK) goto fail;
    }

    s_handle[port] = handle;
    *handle_out = handle;
    ESP_LOGI(TAG, "I2S ready: port=%d %luHz %dbit %s (bclk=%d ws=%d dout=%d din=%d)",
             port, config->sample_rate, config->bits_per_sample,
             config->stereo ? "stereo" : "mono",
             config->bclk_io, config->ws_io, config->dout_io, config->din_io);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(ret));
    if (handle->tx_chan) {
        i2s_channel_disable(handle->tx_chan);
        i2s_del_channel(handle->tx_chan);
    }
    if (handle->rx_chan) {
        i2s_channel_disable(handle->rx_chan);
        i2s_del_channel(handle->rx_chan);
    }
    free(handle);
    return ret;
}

esp_err_t i2sDriver_write(i2sDriver_handle_t handle, const uint8_t *data, size_t bytes, size_t *written, uint32_t timeout_ms)
{
    if (!handle || !handle->tx_chan || !data || !bytes) return ESP_ERR_INVALID_ARG;
    return i2s_channel_write(handle->tx_chan, data, bytes, written, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t i2sDriver_read(i2sDriver_handle_t handle, uint8_t *buf, size_t bytes, size_t *read, uint32_t timeout_ms)
{
    if (!handle || !handle->rx_chan || !buf || !bytes) return ESP_ERR_INVALID_ARG;
    return i2s_channel_read(handle->rx_chan, buf, bytes, read, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t i2sDriver_deinit(i2sDriver_handle_t handle)
{
    if (!handle) return ESP_OK;
    int port = handle->port;
    if (s_handle[port] == handle) s_handle[port] = NULL;
    if (handle->tx_chan) {
        i2s_channel_disable(handle->tx_chan);
        i2s_del_channel(handle->tx_chan);
    }
    if (handle->rx_chan) {
        i2s_channel_disable(handle->rx_chan);
        i2s_del_channel(handle->rx_chan);
    }
    free(handle);
    ESP_LOGI(TAG, "I2S deinit");
    return ESP_OK;
}
