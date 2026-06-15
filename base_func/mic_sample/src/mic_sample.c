/*
 * mic_sample — 麦克风采样 + MQTT 上传
 *
 * 采集 10 秒音频 → 封装 WAV → MQTT 分块上传到服务器
 * 底层使用 es7210_driver 独立驱动 (不依赖 esp_codec_dev)
 */

#include "mic_sample.h"
#include "es7210_drv.h"
#include "cloud_mqtt.h"
#include "pin_map.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "mic_sample";

#define SAMPLE_DURATION_SEC  10
#define CHUNK_SIZE            4096      /* MQTT 每块 4KB */
#define READ_FRAMES           512       /* 每次读 512 帧 (≈32ms@16kHz) */
#define TOPIC_AUDIO_META   "bed/audio_upload_start"
#define TOPIC_AUDIO_CHUNK  "bed/audio_upload_chunk"

/* ── 写入小端值到缓冲区 ─────────────────────────────────────── */
static void writeLE32(uint8_t *dest, uint32_t val)
{
    dest[0] = (uint8_t)(val);
    dest[1] = (uint8_t)(val >> 8);
    dest[2] = (uint8_t)(val >> 16);
    dest[3] = (uint8_t)(val >> 24);
}

static void writeLE16(uint8_t *dest, uint16_t val)
{
    dest[0] = (uint8_t)(val);
    dest[1] = (uint8_t)(val >> 8);
}

/* ── 构建 WAV 头部 (44 字节) ───────────────────────────────── */
static void buildWavHeader(uint8_t *hdr, uint32_t data_size,
                           uint32_t sample_rate, uint16_t bits, uint16_t channels)
{
    uint32_t byte_rate   = sample_rate * channels * (bits / 8);
    uint16_t block_align = (uint16_t)(channels * (bits / 8));
    uint32_t riff_size   = 36 + data_size;

    memcpy(hdr,     "RIFF", 4);
    writeLE32(hdr + 4,  riff_size);
    memcpy(hdr + 8,  "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    writeLE32(hdr + 16, 16);
    writeLE16(hdr + 20, 1);              /* PCM */
    writeLE16(hdr + 22, channels);
    writeLE32(hdr + 24, sample_rate);
    writeLE32(hdr + 28, byte_rate);
    writeLE16(hdr + 32, block_align);
    writeLE16(hdr + 34, bits);
    memcpy(hdr + 36, "data", 4);
    writeLE32(hdr + 40, data_size);
}

/* ── 获取当前时间字符串 ─────────────────────────────────────── */
static void getTimeStr(char *buf, size_t max)
{
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    snprintf(buf, max, "%04d%02d%02d_%02d%02d%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
}

/* ═══════════════════════════════════════════════════════════════
 * 公共 API
 * ═══════════════════════════════════════════════════════════════ */

esp_err_t micSample_start(void *bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    /* ── 1. 初始化 es7210_driver (ES7210 + I2S) ────────────────── */

    es7210_drv_config_t es_cfg = {
        .i2c_bus     = bus,
        .mic_mask    = ES7210_DRV_SEL_MIC1,
        .sample_rate = 16000,
        .pga_gain    = 6,      /* 18dB = 6 × 3dB */
        .total_slots = 2,      /* 标准 I2S 立体声 */
        .mclk_io     = ES7210_I2S_MCLK_PIN,
        .bclk_io     = ES7210_I2S_BCLK_PIN,
        .ws_io       = ES7210_I2S_LRCK_PIN,
        .din_io      = ES7210_I2S_DIN_PIN,
    };

    es7210_drv_handle_t es7210 = NULL;
    esp_err_t err = es7210_drv_init(&es_cfg, &es7210);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es7210_drv_init fail: %s", esp_err_to_name(err));
        return err;
    }

    es7210_drv_dump_regs(es7210);

    /* ── 2. 采样 10 秒, 直存 int16_t PCM ─────────────────────── */

    int total_slots = es_cfg.total_slots;   /* 2 (I2S STD 立体声) */
    int out_channels = 1;                   /* 仅 MIC1, 丢弃右声道 */
    int samples_per_sec = 16000;
    int max_samples = samples_per_sec * SAMPLE_DURATION_SEC;
    int max_bytes   = max_samples * out_channels * sizeof(int16_t);

    int16_t *pcm = heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM);
    if (!pcm) {
        ESP_LOGE(TAG, "PSRAM 分配 PCM 失败");
        es7210_drv_deinit(es7210);
        return ESP_ERR_NO_MEM;
    }

    /* I2S STD 模式: read_buf = READ_FRAMES × total_slots (2ch) */
    int16_t *read_buf = heap_caps_malloc(READ_FRAMES * total_slots * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM);
    if (!read_buf) {
        free(pcm);
        es7210_drv_deinit(es7210);
        ESP_LOGE(TAG, "PSRAM 分配 read_buf 失败");
        return ESP_ERR_NO_MEM;
    }

    int total_samples = 0;
    int empty_reads = 0;
    ESP_LOGI(TAG, "══════ 开始 %d 秒采样 (I2S MONO, 仅 MIC1) ══════", SAMPLE_DURATION_SEC);

    for (int sec = 1; sec <= SAMPLE_DURATION_SEC; sec++) {
        int target = sec * samples_per_sec;
        int32_t min_c = INT32_MAX, max_c = INT32_MIN;

        while (total_samples < target && total_samples < max_samples) {
            int n = es7210_drv_read(es7210, read_buf, READ_FRAMES);
            if (n < 0) {
                ESP_LOGE(TAG, "es7210_drv_read 错误, 中止采样");
                goto sample_fail;
            }
            if (n == 0) {
                empty_reads++;
                if (empty_reads > 15) {
                    ESP_LOGE(TAG, "I2S 连续 %d 次无数据 (>15s)", empty_reads);
                    goto sample_fail;
                }
                continue;
            }
            empty_reads = 0;

            int to_copy = n;
            if (total_samples + to_copy > max_samples)
                to_copy = max_samples - total_samples;

            /* I2S 立体声: slot0=MIC1, 仅提取 slot0 */
            for (int i = 0; i < to_copy; i++) {
                int16_t v = read_buf[i * total_slots];  /* slot 0 only */
                pcm[total_samples + i] = v;
                if (v < min_c) min_c = v;
                if (v > max_c) max_c = v;
            }
            total_samples += to_copy;
        }
        ESP_LOGI(TAG, "[%2ds] samples=%6d  MIC1[%6"PRId32"~%6"PRId32"]",
                 sec, total_samples, min_c, max_c);
    }
    /* ── 3. 封装 WAV ─────────────────────────────────────────── */

    int wav_data_size = total_samples * out_channels * sizeof(int16_t);
    int wav_total     = 44 + wav_data_size;

    uint8_t *wav = NULL;
    if (total_samples > 0) {
        wav = heap_caps_malloc(wav_total, MALLOC_CAP_SPIRAM);
        if (wav) {
            buildWavHeader(wav, (uint32_t)wav_data_size, 16000, 16, out_channels);
            memcpy(wav + 44, pcm, wav_data_size);
            ESP_LOGI(TAG, "WAV 文件: %d 字节 (header+data)", wav_total);
        }
    }
    free(pcm);
    pcm = NULL;

    /* ── 4. MQTT 分块上传 ─────────────────────────────────────── */

    if (wav && total_samples > 0) {
        char id[32];
        getTimeStr(id, sizeof(id));
        int total_chunks = (wav_total + CHUNK_SIZE - 1) / CHUNK_SIZE;

        char meta[256];
        snprintf(meta, sizeof(meta),
                 "{\"type\":\"start\",\"id\":\"%s\",\"size\":%d,\"chunks\":%d}",
                 id, wav_total, total_chunks);
        err = mqttClient_publish(TOPIC_AUDIO_META, meta);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "上传开始: id=%s, size=%d, chunks=%d", id, wav_total, total_chunks);
            vTaskDelay(pdMS_TO_TICKS(200));

            for (int i = 0; i < total_chunks; i++) {
                int offset    = i * CHUNK_SIZE;
                int chunk_len = (offset + CHUNK_SIZE <= wav_total)
                                ? CHUNK_SIZE : (wav_total - offset);
                mqttClient_publishBinary(TOPIC_AUDIO_CHUNK, wav + offset, chunk_len);
                if ((i & 0x1F) == 0) {
                    ESP_LOGI(TAG, "上传 %d/%d 块 (%d%%)", i + 1, total_chunks,
                             (i + 1) * 100 / total_chunks);
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            snprintf(meta, sizeof(meta), "{\"type\":\"end\",\"id\":\"%s\"}", id);
            mqttClient_publish(TOPIC_AUDIO_META, meta);
            vTaskDelay(pdMS_TO_TICKS(200));
            ESP_LOGI(TAG, "══════ 上传完成: %s.wav ══════", id);
        }
        free(wav);
        wav = NULL;
    }

    /* ── 5. 持续采样 (不缓存, 保持 I2S 时钟) ────────────────── */

keep_alive: {
    ESP_LOGI(TAG, "══════ 进入持续采样 (不缓存, I2S 时钟保持) ══════");
    int ka_sec = 0;
    int ka_empty = 0;
    int ka_total_reads = 0;
    int ka_zero_reads = 0;

    while (1) {
        int n = es7210_drv_read(es7210, read_buf, READ_FRAMES);
        ka_total_reads++;
        if (n > 0) {
            if (ka_empty > 0) {
                ESP_LOGI(TAG, "[keep-alive] I2S 数据恢复! 连续空读 %d 次后读到 %d samples",
                         ka_empty, n);
            }
            ka_empty = 0;
        } else if (n == 0) {
            ka_zero_reads++;
            ka_empty++;
            if (ka_empty <= 3 || (ka_empty % 60) == 0) {
                ESP_LOGW(TAG, "[keep-alive %ds] 无数据 (连续 %d 次, 累计空读 %d/%d)",
                         ka_sec, ka_empty, ka_zero_reads, ka_total_reads);
            }
        }
        /* 每秒打一次心跳 */
        int new_sec = ka_total_reads * READ_FRAMES / samples_per_sec;
        if (new_sec > ka_sec) {
            ka_sec = new_sec;
            ESP_LOGI(TAG, "[keep-alive %ds] 已运行, I2S 时钟持续输出", ka_sec);
        }
    }
    /* unreachable — I2S 时钟永不停止 */
}

sample_fail:
    if (pcm) free(pcm);
    /* 即使采集失败也保持 I2S 运行, 方便硬件排查 */
    goto keep_alive;
}
