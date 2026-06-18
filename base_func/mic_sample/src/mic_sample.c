/*
 * mic_sample — 麦克风采样 + MQTT 上传
 * 音频路径照抄 04-audio_es7210: 48kHz TDM 立体声原声直录
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
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

static const char *TAG = "mic_sample";

#define SAMPLE_DURATION_SEC  10
#define CHUNK_SIZE            4096
#define READ_FRAMES           512
#define WARMUP_READS          20
#define TOPIC_AUDIO_META      "bed/audio_upload_start"
#define TOPIC_AUDIO_CHUNK     "bed/audio_upload_chunk"
#define SAMPLE_RATE_HZ        48000
#define OUT_CHANNELS          2

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
    writeLE16(hdr + 20, 1);
    writeLE16(hdr + 22, channels);
    writeLE32(hdr + 24, sample_rate);
    writeLE32(hdr + 28, byte_rate);
    writeLE16(hdr + 32, block_align);
    writeLE16(hdr + 34, bits);
    memcpy(hdr + 36, "data", 4);
    writeLE32(hdr + 40, data_size);
}

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

static void discardWarmup(es7210_drv_handle_t es7210, int16_t *read_buf, int buf_frames)
{
    const int slots = es7210_drv_get_slots(es7210);
    for (int i = 0; i < WARMUP_READS; i++) {
        es7210_drv_read(es7210, read_buf, buf_frames);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    (void)slots;
}

esp_err_t micSample_start(void *bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }

    es7210_drv_config_t es_cfg = {
        .i2c_bus          = bus,
        .mic_mask           = ES7210_DRV_SEL_MIC1,
        .sample_rate        = SAMPLE_RATE_HZ,
        .pga_gain           = 10,
        .total_slots        = OUT_CHANNELS,
        .mclk_io            = ES7210_I2S_MCLK_PIN,
        .bclk_io            = ES7210_I2S_BCLK_PIN,
        .ws_io              = ES7210_I2S_LRCK_PIN,
        .din_io             = ES7210_I2S_DIN_PIN,
        .use_ref_example    = true,
    };

    es7210_drv_handle_t es7210 = NULL;
    esp_err_t err = es7210_drv_init(&es_cfg, &es7210);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es7210_drv_init fail: %s", esp_err_to_name(err));
        return err;
    }

    const int total_slots = es7210_drv_get_slots(es7210);
    const int samples_per_sec = SAMPLE_RATE_HZ;
    const int max_frames = samples_per_sec * SAMPLE_DURATION_SEC;
    const int max_bytes = max_frames * OUT_CHANNELS * (int)sizeof(int16_t);

    int16_t *read_buf = heap_caps_malloc(READ_FRAMES * total_slots * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM);
    if (!read_buf) {
        es7210_drv_deinit(es7210);
        return ESP_ERR_NO_MEM;
    }

    es7210_drv_dump_regs(es7210);
    vTaskDelay(pdMS_TO_TICKS(300));
    discardWarmup(es7210, read_buf, READ_FRAMES);
    ESP_LOGI(TAG, "I2S 预热完成 (slots=%d, DIN=GPIO%d, 参考例程 TDM)",
             total_slots, es7210_drv_get_din_pin(es7210));

    int16_t *pcm = heap_caps_malloc(max_bytes, MALLOC_CAP_SPIRAM);
    if (!pcm) {
        free(read_buf);
        es7210_drv_deinit(es7210);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "══════ 开始 %d 秒采样 (%dHz, %dch 立体声原声) ══════",
             SAMPLE_DURATION_SEC, SAMPLE_RATE_HZ, OUT_CHANNELS);

    int total_frames = 0;
    int empty_reads = 0;
    int32_t min_raw = INT32_MAX, max_raw = INT32_MIN;

    for (int sec = 1; sec <= SAMPLE_DURATION_SEC; sec++) {
        int target = sec * samples_per_sec;
        int32_t sec_min = INT32_MAX, sec_max = INT32_MIN;

        while (total_frames < target && total_frames < max_frames) {
            int n = es7210_drv_read(es7210, read_buf, READ_FRAMES);
            if (n < 0) {
                ESP_LOGE(TAG, "es7210_drv_read 错误, 中止采样");
                goto sample_fail;
            }
            if (n == 0) {
                empty_reads++;
                if (empty_reads > 50) {
                    ESP_LOGE(TAG, "I2S 连续 %d 次无数据", empty_reads);
                    goto sample_fail;
                }
                continue;
            }
            empty_reads = 0;

            if (total_frames + n > max_frames) {
                n = max_frames - total_frames;
            }

            memcpy(&pcm[total_frames * OUT_CHANNELS], read_buf,
                   (size_t)n * OUT_CHANNELS * sizeof(int16_t));

            for (int i = 0; i < n * OUT_CHANNELS; i++) {
                int16_t v = pcm[total_frames * OUT_CHANNELS + i];
                if (v < sec_min) {
                    sec_min = v;
                }
                if (v > sec_max) {
                    sec_max = v;
                }
                if (v < min_raw) {
                    min_raw = v;
                }
                if (v > max_raw) {
                    max_raw = v;
                }
            }

            total_frames += n;
        }
        ESP_LOGI(TAG, "[%2ds] frames=%6d  PCM[%6" PRId32 "~%6" PRId32 "]",
                 sec, total_frames, sec_min, sec_max);
    }

    if (total_frames > 0 && max_raw < 500 && min_raw > -500) {
        ESP_LOGW(TAG, "PCM 幅度极弱, 请检查 ES7210/I2S 接线与 GPIO14 DIN");
    }

    int wav_data_size = total_frames * OUT_CHANNELS * (int)sizeof(int16_t);
    int wav_total = 44 + wav_data_size;

    uint8_t *wav = NULL;
    if (total_frames > 0) {
        wav = heap_caps_malloc(wav_total, MALLOC_CAP_SPIRAM);
        if (wav) {
            buildWavHeader(wav, (uint32_t)wav_data_size, SAMPLE_RATE_HZ, 16, OUT_CHANNELS);
            memcpy(wav + 44, pcm, (size_t)wav_data_size);
            ESP_LOGI(TAG, "WAV 文件: %d 字节 (帧 %d, %dch)", wav_total, total_frames, OUT_CHANNELS);
        }
    }
    free(pcm);
    pcm = NULL;

    if (wav && total_frames > 0) {
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
                int offset = i * CHUNK_SIZE;
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

keep_alive:
    ESP_LOGI(TAG, "══════ 进入持续采样 (I2S 时钟保持) ══════");
    while (1) {
        es7210_drv_read(es7210, read_buf, READ_FRAMES);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

sample_fail:
    if (pcm) {
        free(pcm);
    }
    goto keep_alive;
}
