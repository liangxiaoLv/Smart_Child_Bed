#include "classify_tflite_service.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cloud_mqtt.h"
#include "esp_audio_classifier.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "classify_tflite";

#define CLASSIFY_AUDIO_URL_MAX        256
#define CLASSIFY_HTTP_MAX_BYTES       (1024 * 1024)
#define CLASSIFY_HTTP_READ_BUF_SIZE   4096

static acfg_handle_t s_audio_classifier;
static bool s_audio_classifier_ready = false;
static volatile bool s_audio_classify_busy = false;

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;

    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_len) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static esp_err_t download_http_file(const char *url, uint8_t **out_data, size_t *out_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = CLASSIFY_HTTP_READ_BUF_SIZE,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open audio url failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_len = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "audio http status=%d, url=%s", status_code, url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    if (content_len > CLASSIFY_HTTP_MAX_BYTES) {
        ESP_LOGE(TAG, "audio too large: %" PRId64 " bytes", content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t cap = content_len > 0 ? (size_t)content_len : CLASSIFY_HTTP_MAX_BYTES;
    uint8_t *data = (uint8_t *)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!data) {
        data = (uint8_t *)malloc(cap);
    }
    if (!data) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (total < cap) {
        int r = esp_http_client_read(client, (char *)data + total, cap - total);
        if (r < 0) {
            free(data);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (r == 0) {
            break;
        }
        total += (size_t)r;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total == cap && content_len <= 0) {
        ESP_LOGE(TAG, "audio exceeds max download size");
        free(data);
        return ESP_ERR_INVALID_SIZE;
    }

    *out_data = data;
    *out_len = total;
    return ESP_OK;
}

static esp_err_t wav_to_mono_i16(const uint8_t *wav, size_t wav_len,
                                 int16_t **out_pcm, size_t *out_samples)
{
    if (wav_len < 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t *data_ptr = NULL;
    size_t data_size = 0;

    size_t pos = 12;
    while (pos + 8 <= wav_len) {
        const uint8_t *chunk = wav + pos;
        uint32_t chunk_size = read_le32(chunk + 4);
        size_t chunk_data = pos + 8;
        if (chunk_data + chunk_size > wav_len) {
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            audio_format = read_le16(wav + chunk_data);
            channels = read_le16(wav + chunk_data + 2);
            sample_rate = read_le32(wav + chunk_data + 4);
            bits_per_sample = read_le16(wav + chunk_data + 14);
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_ptr = wav + chunk_data;
            data_size = chunk_size;
        }

        pos = chunk_data + chunk_size + (chunk_size & 1);
    }

    if (audio_format != 1 || channels == 0 || data_ptr == NULL || bits_per_sample != 16) {
        ESP_LOGE(TAG, "unsupported wav: format=%u channels=%u bits=%u",
                 audio_format, channels, bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (sample_rate != ACFG_SAMPLE_RATE) {
        ESP_LOGE(TAG, "wav sample rate must be %d, got %" PRIu32, ACFG_SAMPLE_RATE, sample_rate);
        return ESP_ERR_NOT_SUPPORTED;
    }

    size_t frame_size = channels * 2;
    size_t frames = data_size / frame_size;
    int16_t *pcm = (int16_t *)heap_caps_malloc(frames * sizeof(int16_t),
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm) {
        pcm = (int16_t *)malloc(frames * sizeof(int16_t));
    }
    if (!pcm) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < frames; i++) {
        int32_t sum = 0;
        const uint8_t *frame = data_ptr + i * frame_size;
        for (uint16_t ch = 0; ch < channels; ch++) {
            sum += (int16_t)read_le16(frame + ch * 2);
        }
        pcm[i] = (int16_t)(sum / channels);
    }

    *out_pcm = pcm;
    *out_samples = frames;
    return ESP_OK;
}

static void classify_pcm_i16(const int16_t *pcm, size_t samples)
{
    if (!s_audio_classifier_ready) {
        ESP_LOGE(TAG, "audio classifier not ready");
        return;
    }
    if (samples < ACFG_WINDOW_SAMPLES) {
        ESP_LOGE(TAG, "audio too short: %u samples", (unsigned)samples);
        return;
    }

    int cough_events = 0;
    int cough_state = 0;
    int windows = 0;

    for (size_t start = 0; start + ACFG_WINDOW_SAMPLES <= samples; start += ACFG_HOP_SAMPLES) {
        acfg_result_t result;
        esp_err_t err = acfg_predict_window_i16(&s_audio_classifier,
                                                pcm + start,
                                                ACFG_WINDOW_SAMPLES,
                                                &result);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "classify window failed: %s", esp_err_to_name(err));
            return;
        }

        result.start_sample = (int)start;
        result.end_sample = (int)(start + ACFG_WINDOW_SAMPLES);
        result.start_time_s = (float)start / (float)ACFG_SAMPLE_RATE;
        result.end_time_s = (float)(start + ACFG_WINDOW_SAMPLES) / (float)ACFG_SAMPLE_RATE;

        ESP_LOGI(TAG,
                 "audio classify %.2f-%.2fs: %s prob=%.4f probs=[%.4f, %.4f, %.4f] db=%.2f",
                 result.start_time_s,
                 result.end_time_s,
                 acfg_class_name(result.pred_class),
                 result.probability,
                 result.probs[0],
                 result.probs[1],
                 result.probs[2],
                 result.db);

        if (result.pred_class == ACFG_CLASS_COUGH) {
            if (!cough_state) {
                cough_state = 1;
                cough_events++;
            }
        } else {
            cough_state = 0;
        }

        windows++;
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    ESP_LOGI(TAG, "audio classify done: windows=%d cough_events=%d", windows, cough_events);
}

static void audio_classify_task(void *arg)
{
    char *url = (char *)arg;
    uint8_t *wav = NULL;
    size_t wav_len = 0;
    int16_t *pcm = NULL;
    size_t samples = 0;

    ESP_LOGI(TAG, "download audio for classify: %s", url);
    esp_err_t err = download_http_file(url, &wav, &wav_len);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "downloaded audio: %u bytes", (unsigned)wav_len);
        err = wav_to_mono_i16(wav, wav_len, &pcm, &samples);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "wav parsed: %u samples, %.2fs",
                 (unsigned)samples, (double)samples / ACFG_SAMPLE_RATE);
        classify_pcm_i16(pcm, samples);
    } else {
        ESP_LOGE(TAG, "audio classify request failed: %s", esp_err_to_name(err));
    }

    free(pcm);
    free(wav);
    free(url);
    s_audio_classify_busy = false;
    vTaskDelete(NULL);
}

static void on_mqtt_command(const char *topic, const char *payload)
{
    (void)topic;

    char cmd[48];
    char url[CLASSIFY_AUDIO_URL_MAX];

    if (!json_get_string(payload, "cmd", cmd, sizeof(cmd))) {
        return;
    }
    if (strcmp(cmd, "classify_audio") != 0) {
        return;
    }
    if (!json_get_string(payload, "value", url, sizeof(url)) &&
        !json_get_string(payload, "url", url, sizeof(url))) {
        ESP_LOGE(TAG, "classify_audio missing url/value: %s", payload);
        return;
    }

    if (s_audio_classify_busy) {
        ESP_LOGW(TAG, "audio classify already running");
        return;
    }

    char *task_url = (char *)malloc(strlen(url) + 1);
    if (!task_url) {
        ESP_LOGE(TAG, "no memory for classify url");
        return;
    }
    strcpy(task_url, url);

    s_audio_classify_busy = true;
    BaseType_t ok = xTaskCreatePinnedToCore(audio_classify_task,
                                            "audio_cls",
                                            12288,
                                            task_url,
                                            4,
                                            NULL,
                                            0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "create audio classify task failed");
        s_audio_classify_busy = false;
        free(task_url);
    }
}

esp_err_t classifyTflite_start(void)
{
    static bool started = false;
    if (started) {
        return ESP_OK;
    }

    esp_err_t err = tflm_audio_model_init();
    if (err == ESP_OK) {
        err = acfg_init(&s_audio_classifier,
                        tflm_audio_model_invoke,
                        tflm_audio_model_ctx());
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio classifier init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_audio_classifier_ready = true;
    mqttClient_onCommand(on_mqtt_command);
    started = true;
    ESP_LOGI(TAG, "audio classifier ready");
    return ESP_OK;
}

esp_err_t classifyTflite_predict_i16(const int16_t *mono_samples,
                                     size_t sample_count,
                                     acfg_result_t *out_result)
{
    if (!s_audio_classifier_ready) {
        ESP_LOGE(TAG, "classifier not ready, call classifyTflite_start first");
        return ESP_ERR_INVALID_STATE;
    }
    if (mono_samples == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count != ACFG_WINDOW_SAMPLES) {
        ESP_LOGE(TAG, "invalid sample count: %u, expected %u",
                 (unsigned)sample_count, (unsigned)ACFG_WINDOW_SAMPLES);
        return ESP_ERR_INVALID_SIZE;
    }

    return acfg_predict_window_i16(&s_audio_classifier,
                                   mono_samples,
                                   sample_count,
                                   out_result);
}
