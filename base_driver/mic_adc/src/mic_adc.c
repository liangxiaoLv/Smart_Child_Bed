/*
 * mic_adc — 麦克风 ADC 驱动 (基于 esp_codec_dev 官方库)
 *
 * 封装 ES7210 初始化 + I2S 数据读取，提供简洁的 int16_t 采样接口。
 */

#include "mic_adc.h"
#include "pin_map_yt_demo.h"

#include "es7210_adc.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "mic_adc";

typedef struct {
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_if_t      *codec_if;
    const audio_codec_data_if_t *data_if;
    esp_codec_dev_handle_t       dev;
    i2s_chan_handle_t            tx_chan;   /* 仅用于产生 MCLK, 不传给 esp_codec_dev */
    i2s_chan_handle_t            rx_chan;
} mic_adc_t;

esp_err_t mic_adc_init(const mic_adc_config_t *cfg, mic_adc_handle_t *handle)
{
    if (!cfg || !handle) return ESP_ERR_INVALID_ARG;

    mic_adc_t *adc = calloc(1, sizeof(mic_adc_t));
    if (!adc) return ESP_ERR_NO_MEM;

    esp_err_t ret = ESP_OK;

    /* ── 1. 创建 I2S 双工通道: TX=STD(驱时钟), RX=STD(收 ES7210 I2S 数据) ── */
    i2s_chan_config_t chan_cfg = {
        .id                 = I2S_NUM_0,
        .role               = I2S_ROLE_MASTER,
        .dma_desc_num       = 3,
        .dma_frame_num      = 240,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority      = 0,
    };
    ret = i2s_new_channel(&chan_cfg, &adc->tx_chan, &adc->rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel fail: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* TX: STD 模式 (只需 MCLK/BCLK/LRCK, 2 slot × 16bit = 32 BCLK/帧) */
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = cfg->sample_rate,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = I2S_STD_SLOT_BOTH,
            .ws_width       = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = true,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk    = cfg->mclk_io,
            .bclk    = cfg->bclk_io,
            .ws      = cfg->ws_io,
            .dout    = I2S_GPIO_UNUSED,
            .din     = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(adc->tx_chan, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(TX) fail: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* RX: STD 模式 (2 slot × 16bit = 32 BCLK/帧, 与 TX 完全对称) */
    i2s_std_config_t rx_std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = cfg->sample_rate,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
            .slot_mode      = I2S_SLOT_MODE_STEREO,
            .slot_mask      = I2S_STD_SLOT_BOTH,
            .ws_width       = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol         = false,
            .bit_shift      = true,
            .left_align     = false,
            .big_endian     = false,
            .bit_order_lsb  = false,
        },
        .gpio_cfg = {
            .mclk    = I2S_GPIO_UNUSED,
            .bclk    = I2S_GPIO_UNUSED,
            .ws      = I2S_GPIO_UNUSED,
            .dout    = I2S_GPIO_UNUSED,
            .din     = cfg->din_io,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ret = i2s_channel_init_std_mode(adc->rx_chan, &rx_std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(RX) fail: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* TX+RX 同时使能 */
    ret = i2s_channel_enable(adc->tx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(TX) fail: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = i2s_channel_enable(adc->rx_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(RX) fail: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* ── 2. 创建 I2C 控制接口 ────────────────────────────────────── */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = 0,   /* I2C_NUM_0 */
        .addr       = ES7210_I2C_ADDR,
        .bus_handle = cfg->i2c_bus,
    };
    adc->ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!adc->ctrl_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2c_ctrl fail");
        ret = ESP_FAIL;
        goto fail;
    }

    /* ── 3. 创建 ES7210 codec ───────────────────────────────────── */
    es7210_codec_cfg_t es_cfg = {
        .ctrl_if     = adc->ctrl_if,
        .mic_selected = cfg->mic_mask,
        .master_mode  = false,       /* ES7210 为 I2S Slave */
        .mclk_div     = 256,
    };
    adc->codec_if = es7210_codec_new(&es_cfg);
    if (!adc->codec_if) {
        ESP_LOGE(TAG, "es7210_codec_new fail");
        ret = ESP_FAIL;
        goto fail;
    }

    /* ── 4. 设置增益 (30dB) ────────────────────────────────────── */
    adc->codec_if->set_mic_gain(adc->codec_if, 30.0f);

    /* ── 6. 绑定 I2S 数据接口 ────────────────────────────────────── */
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port      = I2S_NUM_0,
        .rx_handle = adc->rx_chan,
        .tx_handle = adc->tx_chan,
    };
    adc->data_if = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (!adc->data_if) {
        ESP_LOGE(TAG, "audio_codec_new_i2s_data fail");
        ret = ESP_FAIL;
        goto fail;
    }

    /* ── 7. 创建输入设备并启动 ───────────────────────────────────── */
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type  = ESP_CODEC_DEV_TYPE_IN,
        .codec_if  = adc->codec_if,
        .data_if   = adc->data_if,
    };
    adc->dev = esp_codec_dev_new(&dev_cfg);
    if (!adc->dev) {
        ESP_LOGE(TAG, "esp_codec_dev_new fail");
        ret = ESP_FAIL;
        goto fail;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel         = (uint8_t)cfg->channel_num,
        .channel_mask    = 0,
        .sample_rate     = cfg->sample_rate,
        .mclk_multiple   = 0,
    };
    ret = esp_codec_dev_open(adc->dev, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_codec_dev_open fail: %s", esp_err_to_name(ret));
        goto fail;
    }

    /* dump ES7210 关键寄存器, 确认芯片状态 */
    adc->codec_if->dump_reg(adc->codec_if);

    ESP_LOGI(TAG, "初始化完成: SR=%"PRIu32"Hz, ch=%d, mic_mask=0x%02X",
             cfg->sample_rate, cfg->channel_num, cfg->mic_mask);

    *handle = adc;
    return ESP_OK;

fail:
    if (adc->rx_chan) i2s_del_channel(adc->rx_chan);
    if (adc->tx_chan) i2s_del_channel(adc->tx_chan);
    if (adc->ctrl_if) audio_codec_delete_ctrl_if(adc->ctrl_if);
    free(adc);
    return ret;
}

int mic_adc_read(mic_adc_handle_t handle, int16_t *buf, int samples)
{
    if (!handle || !buf || samples <= 0) return -1;

    mic_adc_t *adc = (mic_adc_t *)handle;
    static int zero_cnt = 0;
    int ret = esp_codec_dev_read(adc->dev, buf, samples * sizeof(int16_t));
    if (ret < 0) {
        ESP_LOGE(TAG, "read error: %d", ret);
        return -1;
    }
    if (ret == 0) {
        zero_cnt++;
        if (zero_cnt <= 3 || (zero_cnt % 10) == 0) {
            ESP_LOGW(TAG, "read 0 bytes (第 %d 次超时, I2S 无数据)", zero_cnt);
        }
        return 0;
    }
    zero_cnt = 0;
    return ret / sizeof(int16_t);
}

esp_err_t mic_adc_deinit(mic_adc_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    mic_adc_t *adc = (mic_adc_t *)handle;

    if (adc->dev) {
        esp_codec_dev_close(adc->dev);
        esp_codec_dev_delete(adc->dev);
    }
    if (adc->data_if)  audio_codec_delete_data_if(adc->data_if);
    if (adc->codec_if) audio_codec_delete_codec_if(adc->codec_if);
    if (adc->ctrl_if)  audio_codec_delete_ctrl_if(adc->ctrl_if);
    if (adc->rx_chan)  i2s_del_channel(adc->rx_chan);
    if (adc->tx_chan)  i2s_del_channel(adc->tx_chan);

    free(adc);
    return ESP_OK;
}
