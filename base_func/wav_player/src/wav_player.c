#include "wav_player.h"
#include "i2s_driver.h"
#include "aw883xx_driver.h"
#include "pin_map.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>

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
static i2sDriver_handle_t s_i2s;
static TaskHandle_t s_play_task;
static const uint8_t *s_pcm_data;
static size_t s_pcm_len;
static bool s_src_mono;
static volatile bool s_stop;
static uint8_t s_volume_pct = 80;
/* 记录当前 I2S 已配置的参数，播放时比较，相同则无需重配 */
static uint32_t s_cur_sample_rate;
static uint8_t  s_cur_bits;

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

    /* 扫描 PCM 峰值（只做一次） */
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

    uint32_t loop_cnt = 0;
    #define MONO2ST_SAMPLES 256
    int16_t stereo_buf[MONO2ST_SAMPLES * 2];

    while (!s_stop) {
        loop_cnt++;
        ESP_LOGI(TAG, "循环播放第 %lu 次", (unsigned long)loop_cnt);

        const uint8_t *ptr = s_pcm_data;
        size_t remaining = s_pcm_len;

        if (s_src_mono) {
            /* 单声道 → 立体声 */
            while (!s_stop && remaining >= 2) {
                size_t batch = remaining / 2;
                if (batch > MONO2ST_SAMPLES) batch = MONO2ST_SAMPLES;
                for (size_t i = 0; i < batch; i++) {
                    int16_t s = (int16_t)(ptr[i * 2] | ((uint16_t)ptr[i * 2 + 1] << 8));
                    stereo_buf[i * 2]     = s;
                    stereo_buf[i * 2 + 1] = s;
                }
                size_t written = 0;
                esp_err_t ret = i2sDriver_write(s_i2s, (uint8_t *)stereo_buf, batch * 4, &written, 1000);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "I2S 写入失败: %s (batch=%u)", esp_err_to_name(ret), (unsigned)(batch * 4));
                    s_stop = true;
                    break;
                }
                if (written != batch * 4) {
                    ESP_LOGW(TAG, "I2S 部分写入: %u/%u", (unsigned)written, (unsigned)(batch * 4));
                }
                ptr += batch * 2;
                remaining -= batch * 2;
            }
        } else {
            while (!s_stop && remaining > 0) {
                size_t n = remaining < 2048 ? remaining : 2048;
                size_t written = 0;
                esp_err_t ret = i2sDriver_write(s_i2s, ptr, n, &written, 1000);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "I2S 写入失败: %s (n=%u)", esp_err_to_name(ret), (unsigned)n);
                    s_stop = true;
                    break;
                }
                if (written != n) {
                    ESP_LOGW(TAG, "I2S 部分写入: %u/%u", (unsigned)written, (unsigned)n);
                }
                ptr += n;
                remaining -= n;
            }
        }
    }

    ESP_LOGI(TAG, "播放结束");
    s_play_task = NULL;
    vTaskDelete(NULL);
}

/* ─── 公共 API ────────────────────────────────────────────── */

esp_err_t wavPlayer_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    esp_err_t ret = aw883xx_init(bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AW88399QNR 初始化失败");
        return ret;
    }

    /* I2S 随 init 一起初始化，此后常驻，无需每次播放重建 */
    i2sDriver_config_t cfg = I2S_DRIVER_DEFAULT_CONFIG();
    cfg.stereo   = true;
    cfg.mclk_io  = -1;
    cfg.bclk_io  = I2S1_BCK_PIN;
    cfg.ws_io    = I2S1_LRCK_PIN;
    cfg.dout_io  = I2S1_DOUT_PIN;

    ret = i2sDriver_init(I2S1_PORT_NUM, &cfg, &s_i2s);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S1 初始化失败");
        return ret;
    }
    s_cur_sample_rate = cfg.sample_rate;
    s_cur_bits        = cfg.bits_per_sample;
    return ESP_OK;
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

    if (!s_i2s) {
        /* 异常情况：init 未调用或已被外部 deinit，重新初始化 */
        i2sDriver_config_t cfg = I2S_DRIVER_DEFAULT_CONFIG();
        cfg.sample_rate     = sample_rate;
        cfg.bits_per_sample = bits_per_sample;
        cfg.stereo          = true;
        cfg.mclk_io         = -1;
        cfg.bclk_io         = I2S1_BCK_PIN;
        cfg.ws_io           = I2S1_LRCK_PIN;
        cfg.dout_io         = I2S1_DOUT_PIN;
        esp_err_t ret = i2sDriver_init(I2S1_PORT_NUM, &cfg, &s_i2s);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S1 初始化失败");
            return ret;
        }
        s_cur_sample_rate = sample_rate;
        s_cur_bits        = (uint8_t)bits_per_sample;
    } else if (sample_rate != s_cur_sample_rate || (uint8_t)bits_per_sample != s_cur_bits) {
        /* 参数变化，仅重配时钟，无需重建通道 */
        esp_err_t ret = i2sDriver_reconfigClock(s_i2s, sample_rate, bits_per_sample, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S1 重配时钟失败");
            return ret;
        }
        s_cur_sample_rate = sample_rate;
        s_cur_bits        = (uint8_t)bits_per_sample;
    }
    /* 参数与上次相同：直接复用已有通道，无需任何 init/reconfig */

    /* 同步采样率到 AW88399QNR 的 I2SCTRL1 */
    aw883xx_setSampleRate(sample_rate);

    aw883xx_setVolume(s_volume_pct);

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
    }
    return ESP_OK;
}

esp_err_t wavPlayer_setVolume(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_volume_pct = pct;
    return aw883xx_setVolume(pct);
}

bool wavPlayer_isPlaying(void)
{
    return s_play_task != NULL;
}

/* ─── 测试正弦波（绕过 WAV 解析） ────────────────────────── */
#define TONE_AMPLITUDE   20000  /* 峰值 ~60% 满幅，避免削波 */
#define TONE_CHUNK_MS    50     /* 每次写 50ms 数据 */

esp_err_t wavPlayer_testTone(uint32_t freq_hz, uint32_t duration_ms, uint32_t sample_rate)
{
    if (!s_i2s) {
        ESP_LOGE(TAG, "I2S 未初始化，请先调用 wavPlayer_init");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_play_task) {
        ESP_LOGW(TAG, "正在播放中，先停止");
        wavPlayer_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 若未指定采样率，沿用当前 I2S 配置 */
    if (sample_rate == 0) {
        sample_rate = s_cur_sample_rate;
    }

    /* 按需重配 I2S 时钟 */
    if (sample_rate != s_cur_sample_rate || 16 != s_cur_bits) {
        esp_err_t ret = i2sDriver_reconfigClock(s_i2s, sample_rate, 16, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "testTone 重配时钟失败");
            return ret;
        }
        s_cur_sample_rate = sample_rate;
        s_cur_bits = 16;
    }

    aw883xx_setVolume(s_volume_pct);

    /* 每次生成一小段正弦波，直接写入 I2S，不经过 playTask */
    size_t chunk_samples = (size_t)sample_rate * TONE_CHUNK_MS / 1000;
    size_t chunk_bytes   = chunk_samples * 4;  /* stereo 16-bit */
    int16_t *buf = malloc(chunk_bytes);
    if (!buf) {
        ESP_LOGE(TAG, "testTone malloc(%u) 失败", (unsigned)chunk_bytes);
        return ESP_ERR_NO_MEM;
    }

    const double two_pi_f = 2.0 * M_PI * (double)freq_hz / (double)sample_rate;
    uint32_t elapsed_ms = 0;
    size_t phase = 0;  /* 跨 chunk 保持相位连续 */

    ESP_LOGI(TAG, "testTone: %luHz %lums sr=%lu chunk=%u samples",
             (unsigned long)freq_hz, (unsigned long)duration_ms,
             (unsigned long)sample_rate, (unsigned)chunk_samples);

    esp_err_t ret = ESP_OK;
    while (elapsed_ms < duration_ms) {
        uint32_t remain_ms = duration_ms - elapsed_ms;
        size_t n_samples = chunk_samples;
        if (n_samples > (size_t)sample_rate * remain_ms / 1000) {
            n_samples = (size_t)sample_rate * remain_ms / 1000;
        }
        if (n_samples == 0) break;

        for (size_t i = 0; i < n_samples; i++) {
            int16_t s = (int16_t)(TONE_AMPLITUDE * sin(two_pi_f * (double)(phase + i)));
            buf[i * 2]     = s;
            buf[i * 2 + 1] = s;
        }
        phase += n_samples;

        size_t written = 0;
        size_t bytes = n_samples * 4;
        ret = i2sDriver_write(s_i2s, (uint8_t *)buf, bytes, &written, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "testTone I2S 写入失败: %s", esp_err_to_name(ret));
            break;
        }
        if (written != bytes) {
            ESP_LOGW(TAG, "testTone 部分写入: %u/%u", (unsigned)written, (unsigned)bytes);
        }

        elapsed_ms += (uint32_t)(n_samples * 1000 / sample_rate);
    }

    free(buf);

    /* 恢复 I2S 到默认采样率（下次播 WAV 时会按需重配） */
    ESP_LOGI(TAG, "testTone 播放完成 (ret=%s)", esp_err_to_name(ret));
    return ret;
}
