#include "wav_player.h"
#include "i2s_driver.h"
#include "xl9555_driver.h"
#include "es8388_driver.h"
#include "pin_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wav";

/* ─── WAV 文件头结构 (44 字节标准 PCM) ──────────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint8_t  riff[4];
    uint32_t file_size;
    uint8_t  wave[4];
    uint8_t  fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} wavHeader_t;
#pragma pack(pop)

/* ─── 全局播放状态 ────────────────────────────────────────── */
static TaskHandle_t s_play_task;
static const uint8_t *s_pcm_data;
static size_t s_pcm_len;
static bool s_src_mono;
static volatile bool s_stop;
static uint8_t s_volume_pct = 80;

/* ─── 解析 WAV 头 ─────────────────────────────────────────── */
static bool parseWavHeader(const uint8_t *data, size_t len,
                           uint32_t *sample_rate, uint16_t *num_channels,
                           uint16_t *bits_per_sample,
                           const uint8_t **pcm_data, size_t *pcm_len)
{
    if (len < 44) {
        ESP_LOGE(TAG, "数据太短，不是 WAV 文件");
        return false;
    }

    wavHeader_t *hdr = (wavHeader_t *)data;

    if (memcmp(hdr->riff, "RIFF", 4) || memcmp(hdr->wave, "WAVE", 4) ||
        memcmp(hdr->fmt,  "fmt ", 4)) {
        ESP_LOGE(TAG, "不是有效 WAV 文件");
        ESP_LOG_BUFFER_HEX(TAG, data, 16);
        return false;
    }

    if (hdr->audio_format != 1) {
        ESP_LOGE(TAG, "不是 PCM 格式 (format=%u)", hdr->audio_format);
        return false;
    }

    *sample_rate    = hdr->sample_rate;
    *num_channels   = hdr->num_channels;
    *bits_per_sample = hdr->bits_per_sample;

    /* 跳过扩展 fmt 块，查找 data 块 */
    size_t off = 20 + hdr->fmt_size;
    while (off + 8 <= len) {
        if (memcmp(data + off, "data", 4) == 0) {
            uint32_t ds = (uint32_t)data[off + 4] |
                          ((uint32_t)data[off + 5] << 8) |
                          ((uint32_t)data[off + 6] << 16) |
                          ((uint32_t)data[off + 7] << 24);
            *pcm_data = data + off + 8;
            *pcm_len  = ds;
            if (*pcm_len > len - off - 8) *pcm_len = len - off - 8;
            return true;
        }
        off += 8 + ((uint32_t)data[off + 4] |
                    ((uint32_t)data[off + 5] << 8) |
                    ((uint32_t)data[off + 6] << 16) |
                    ((uint32_t)data[off + 7] << 24));
    }

    /* fallback: 标准 44 字节头 */
    ESP_LOGW(TAG, "未找到 data 块，使用默认偏移 44");
    *pcm_data = data + 44;
    *pcm_len  = len - 44;
    return true;
}

/* ─── 播放任务 ────────────────────────────────────────────── */
static void playTask(void *arg)
{
    (void)arg;
    s_stop = false;

    /* 扫描 PCM 峰值 */
    int16_t peak = 0;
    size_t sample_count = s_pcm_len / 2;
    const int16_t *samples = (const int16_t *)s_pcm_data;
    for (size_t i = 0; i < sample_count && i < 65536; i++) {
        int16_t v = samples[i];
        if (v > peak) peak = v;
        if (-v > peak) peak = -v;
    }
    ESP_LOGI(TAG, "PCM 峰值=%d, 前8样本: %d %d %d %d %d %d %d %d",
             peak,
             samples[0], samples[1], samples[2], samples[3],
             samples[4], samples[5], samples[6], samples[7]);

    xl9555Driver_setPin(XL9555_PORT0, XL9555_SPK_EN, false);

    const uint8_t *ptr = s_pcm_data;
    size_t remaining = s_pcm_len;

    if (s_src_mono) {
        /* 单声道 → 立体声：每个 int16 样本写两次（左右声道相同） */
        while (!s_stop && remaining >= 2) {
            int16_t stereo[2];
            stereo[0] = stereo[1] = (int16_t)(ptr[0] | (ptr[1] << 8));
            i2sDriver_write((uint8_t *)stereo, 4, NULL, 1000);
            ptr += 2;
            remaining -= 2;
        }
    } else {
        while (!s_stop && remaining > 0) {
            size_t n = remaining < 2048 ? remaining : 2048;
            i2sDriver_write(ptr, n, NULL, 1000);
            ptr += n;
            remaining -= n;
        }
    }

    xl9555Driver_setPin(XL9555_PORT0, XL9555_SPK_EN, true);
    ESP_LOGI(TAG, "播放结束");
    s_play_task = NULL;
    vTaskDelete(NULL);
}

/* ─── 公共 API ────────────────────────────────────────────── */

esp_err_t wavPlayer_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = es8388Driver_init(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 初始化失败");
    }
    return ret;
}

esp_err_t wavPlayer_play(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    if (s_play_task) {
        ESP_LOGW(TAG, "正在播放中，先停止");
        wavPlayer_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    uint32_t sample_rate;
    uint16_t num_channels, bits_per_sample;

    if (!parseWavHeader(data, len, &sample_rate, &num_channels,
                        &bits_per_sample, &s_pcm_data, &s_pcm_len)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_src_mono = (num_channels == 1);
    ESP_LOGI(TAG, "WAV: %luHz %uch %ubit, PCM %u bytes",
             sample_rate, num_channels, bits_per_sample, (unsigned)s_pcm_len);

    i2sDriver_deinit();

    es8388Driver_setVolume((uint8_t)((s_volume_pct * 33) / 100));

    /* DAC 通常需要立体声 I2S 格式，单声道源数据写入时复制到左右声道 */
    i2sDriver_config_t cfg = I2S_DRIVER_DEFAULT_CONFIG();
    cfg.sample_rate    = sample_rate;
    cfg.bits_per_sample = bits_per_sample;
    cfg.stereo         = true;
    cfg.mclk_io        = I2S_MCLK_PIN;
    cfg.bclk_io        = I2S_BCLK_PIN;
    cfg.ws_io          = I2S_WS_PIN;
    cfg.dout_io        = I2S_DOUT_PIN;

    esp_err_t ret = i2sDriver_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S 初始化失败");
        return ret;
    }

    BaseType_t r = xTaskCreate(playTask, "wav_play", 4096,
                               NULL, 5, &s_play_task);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "创建播放任务失败");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wavPlayer_stop(void)
{
    s_stop = true;
    int timeout = 50;
    while (s_play_task && --timeout) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_play_task) {
        vTaskDelete(s_play_task);
        s_play_task = NULL;
        xl9555Driver_setPin(XL9555_PORT0, XL9555_SPK_EN, true);
    }
    return ESP_OK;
}

esp_err_t wavPlayer_setVolume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_volume_pct = pct;
    uint8_t vol = (uint8_t)((pct * 33) / 100);
    return es8388Driver_setVolume(vol);
}

bool wavPlayer_isPlaying(void)
{
    return s_play_task != NULL;
}
