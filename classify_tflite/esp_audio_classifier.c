#include "esp_audio_classifier.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define ACFG_MEL_DB_MEAN       (-15.863933460836352f)
#define ACFG_MEL_DB_STD        (34.02759945938546f)
#define ACFG_AMP_TO_DB_AMIN    (1.0e-10f)
#define ACFG_TOP_DB            (80.0f)
#define ACFG_PI                (3.14159265358979323846f)
#define ACFG_TAG               "audio_cls"

static float hz_to_mel(float hz)
{
    return 2595.0f * log10f(1.0f + hz / 700.0f);
}

static float mel_to_hz(float mel)
{
    return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

static int reflect_index(int idx, int len)
{
    if (len <= 1) {
        return 0;
    }

    while (idx < 0 || idx >= len) {
        if (idx < 0) {
            idx = -idx;
        } else {
            idx = 2 * len - 2 - idx;
        }
    }

    return idx;
}

static void fft_radix2(float *buf, int n)
{
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        while ((j & bit) != 0) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if (i < j) {
            float tr = buf[2 * i];
            float ti = buf[2 * i + 1];
            buf[2 * i] = buf[2 * j];
            buf[2 * i + 1] = buf[2 * j + 1];
            buf[2 * j] = tr;
            buf[2 * j + 1] = ti;
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * ACFG_PI / (float)len;
        float wlen_r = cosf(ang);
        float wlen_i = sinf(ang);

        for (int i = 0; i < n; i += len) {
            float wr = 1.0f;
            float wi = 0.0f;
            int half = len >> 1;

            for (int k = 0; k < half; ++k) {
                int even = i + k;
                int odd = even + half;

                float ur = buf[2 * even];
                float ui = buf[2 * even + 1];
                float vr = buf[2 * odd] * wr - buf[2 * odd + 1] * wi;
                float vi = buf[2 * odd] * wi + buf[2 * odd + 1] * wr;

                buf[2 * even] = ur + vr;
                buf[2 * even + 1] = ui + vi;
                buf[2 * odd] = ur - vr;
                buf[2 * odd + 1] = ui - vi;

                float next_wr = wr * wlen_r - wi * wlen_i;
                float next_wi = wr * wlen_i + wi * wlen_r;
                wr = next_wr;
                wi = next_wi;
            }
        }
    }
}

static void build_hann_window(float *window)
{
    for (int i = 0; i < ACFG_N_FFT; ++i) {
        window[i] = 0.5f - 0.5f * cosf(2.0f * ACFG_PI * (float)i / (float)ACFG_N_FFT);
    }
}

static void build_mel_fbanks(float *fbanks)
{
    const int mel_points_count = ACFG_N_MELS + 2;
    float mel_points[ACFG_N_MELS + 2];

    float min_mel = hz_to_mel(0.0f);
    float max_mel = hz_to_mel((float)ACFG_SAMPLE_RATE / 2.0f);

    for (int i = 0; i < mel_points_count; ++i) {
        float mel = min_mel + (max_mel - min_mel) * (float)i / (float)(mel_points_count - 1);
        mel_points[i] = mel_to_hz(mel);
    }

    memset(fbanks, 0, sizeof(float) * ACFG_N_MELS * ACFG_FFT_BINS);

    for (int m = 0; m < ACFG_N_MELS; ++m) {
        float left = mel_points[m];
        float center = mel_points[m + 1];
        float right = mel_points[m + 2];

        for (int k = 0; k < ACFG_FFT_BINS; ++k) {
            float freq = (float)k * (float)ACFG_SAMPLE_RATE / (float)ACFG_N_FFT;
            float weight = 0.0f;

            if (freq >= left && freq <= center && center > left) {
                weight = (freq - left) / (center - left);
            } else if (freq > center && freq <= right && right > center) {
                weight = (right - freq) / (right - center);
            }

            fbanks[m * ACFG_FFT_BINS + k] = weight;
        }
    }
}

static float calc_db_f32(const float *samples, size_t sample_count)
{
    if (samples == NULL || sample_count == 0) {
        return -180.0f;
    }

    double sum_sq = 0.0;
    for (size_t i = 0; i < sample_count; ++i) {
        sum_sq += (double)samples[i] * (double)samples[i];
    }

    float rms = sqrtf((float)(sum_sq / (double)sample_count));
    return 20.0f * log10f(rms + 1.0e-9f);
}

static void softmax3(const float *logits, float *probs)
{
    float max_v = logits[0];
    if (logits[1] > max_v) {
        max_v = logits[1];
    }
    if (logits[2] > max_v) {
        max_v = logits[2];
    }

    float e0 = expf(logits[0] - max_v);
    float e1 = expf(logits[1] - max_v);
    float e2 = expf(logits[2] - max_v);
    float sum = e0 + e1 + e2;

    probs[0] = e0 / sum;
    probs[1] = e1 / sum;
    probs[2] = e2 / sum;
}

static void *acfg_alloc(size_t size)
{
#ifdef ESP_PLATFORM
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != NULL) {
        return ptr;
    }
#endif
    return malloc(size);
}

const char *acfg_class_name(int class_id)
{
    switch (class_id) {
    case ACFG_CLASS_OTHER:
        return "other";
    case ACFG_CLASS_COUGH:
        return "cough";
    case ACFG_CLASS_CRYING:
        return "crying";
    default:
        return "unknown";
    }
}

esp_err_t acfg_init(acfg_handle_t *handle,
                    acfg_invoke_fn_t invoke,
                    void *invoke_ctx)
{
    if (handle == NULL || invoke == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle, 0, sizeof(*handle));
    handle->invoke = invoke;
    handle->invoke_ctx = invoke_ctx;

    handle->hann_window = (float *)acfg_alloc(sizeof(float) * ACFG_N_FFT);
    handle->mel_fbanks = (float *)acfg_alloc(sizeof(float) * ACFG_N_MELS * ACFG_FFT_BINS);
    handle->fft_buf = (float *)acfg_alloc(sizeof(float) * ACFG_N_FFT * 2);
    handle->power_spec = (float *)acfg_alloc(sizeof(float) * ACFG_FFT_BINS);
    handle->features = (float *)acfg_alloc(sizeof(float) * ACFG_FEATURE_COUNT);
    handle->pcm_f32 = (float *)acfg_alloc(sizeof(float) * ACFG_WINDOW_SAMPLES);

    if (handle->hann_window == NULL ||
        handle->mel_fbanks == NULL ||
        handle->fft_buf == NULL ||
        handle->power_spec == NULL ||
        handle->features == NULL ||
        handle->pcm_f32 == NULL) {
        acfg_deinit(handle);
        return ESP_ERR_NO_MEM;
    }

    build_hann_window(handle->hann_window);
    build_mel_fbanks(handle->mel_fbanks);

#ifdef ESP_PLATFORM
    size_t bytes =
        sizeof(float) * ACFG_N_FFT +
        sizeof(float) * ACFG_N_MELS * ACFG_FFT_BINS +
        sizeof(float) * ACFG_N_FFT * 2 +
        sizeof(float) * ACFG_FFT_BINS +
        sizeof(float) * ACFG_FEATURE_COUNT +
        sizeof(float) * ACFG_WINDOW_SAMPLES;
    ESP_LOGI(ACFG_TAG, "preprocess buffers allocated: %u bytes", (unsigned)bytes);
#endif

    return ESP_OK;
}

void acfg_deinit(acfg_handle_t *handle)
{
    if (handle == NULL) {
        return;
    }

    free(handle->hann_window);
    free(handle->mel_fbanks);
    free(handle->fft_buf);
    free(handle->power_spec);
    free(handle->features);
    free(handle->pcm_f32);
    memset(handle, 0, sizeof(*handle));
}

esp_err_t acfg_preprocess_f32(acfg_handle_t *handle,
                              const float *mono_samples,
                              size_t sample_count,
                              float *out_features,
                              size_t out_feature_count)
{
    if (handle == NULL || mono_samples == NULL || out_features == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count != ACFG_WINDOW_SAMPLES || out_feature_count != ACFG_FEATURE_COUNT) {
        return ESP_ERR_INVALID_SIZE;
    }

    float max_db = -100000.0f;

    for (int frame = 0; frame < ACFG_N_FRAMES; ++frame) {
        int frame_start = frame * ACFG_SPEC_HOP_LENGTH - ACFG_N_FFT / 2;

        for (int n = 0; n < ACFG_N_FFT; ++n) {
            int src = reflect_index(frame_start + n, (int)sample_count);
            handle->fft_buf[2 * n] = mono_samples[src] * handle->hann_window[n];
            handle->fft_buf[2 * n + 1] = 0.0f;
        }

        fft_radix2(handle->fft_buf, ACFG_N_FFT);

        for (int k = 0; k < ACFG_FFT_BINS; ++k) {
            float re = handle->fft_buf[2 * k];
            float im = handle->fft_buf[2 * k + 1];
            handle->power_spec[k] = re * re + im * im;
        }

        for (int mel = 0; mel < ACFG_N_MELS; ++mel) {
            float mel_power = 0.0f;
            const float *weights = &handle->mel_fbanks[mel * ACFG_FFT_BINS];

            for (int k = 0; k < ACFG_FFT_BINS; ++k) {
                mel_power += handle->power_spec[k] * weights[k];
            }

            float db = 10.0f * log10f(fmaxf(mel_power, ACFG_AMP_TO_DB_AMIN));
            out_features[mel * ACFG_N_FRAMES + frame] = db;

            if (db > max_db) {
                max_db = db;
            }
        }

#ifdef ESP_PLATFORM
        if ((frame & 0x07) == 0x07) {
            taskYIELD();
        }
#endif
    }

    float min_db = max_db - ACFG_TOP_DB;
    for (int i = 0; i < ACFG_FEATURE_COUNT; ++i) {
        float db = out_features[i];
        if (db < min_db) {
            db = min_db;
        }
        out_features[i] = (db - ACFG_MEL_DB_MEAN) / (ACFG_MEL_DB_STD + 1.0e-6f);
    }

    return ESP_OK;
}

esp_err_t acfg_predict_window_f32(acfg_handle_t *handle,
                                  const float *mono_samples,
                                  size_t sample_count,
                                  acfg_result_t *out_result)
{
    if (handle == NULL || mono_samples == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = acfg_preprocess_f32(handle,
                                        mono_samples,
                                        sample_count,
                                        handle->features,
                                        ACFG_FEATURE_COUNT);
    if (err != ESP_OK) {
        return err;
    }

    float logits[ACFG_CLASS_COUNT] = {0};
    err = handle->invoke(handle->features,
                         ACFG_FEATURE_COUNT,
                         logits,
                         ACFG_CLASS_COUNT,
                         handle->invoke_ctx);
    if (err != ESP_OK) {
        return err;
    }

    memset(out_result, 0, sizeof(*out_result));
    softmax3(logits, out_result->probs);

    out_result->pred_class = 0;
    out_result->probability = out_result->probs[0];
    for (int i = 1; i < ACFG_CLASS_COUNT; ++i) {
        if (out_result->probs[i] > out_result->probability) {
            out_result->probability = out_result->probs[i];
            out_result->pred_class = i;
        }
    }
    out_result->start_sample = 0;
    out_result->end_sample = (int)sample_count;
    out_result->start_time_s = 0.0f;
    out_result->end_time_s = (float)sample_count / (float)ACFG_SAMPLE_RATE;
    out_result->db = calc_db_f32(mono_samples, sample_count);

    return ESP_OK;
}

esp_err_t acfg_predict_window_i16(acfg_handle_t *handle,
                                  const int16_t *mono_samples,
                                  size_t sample_count,
                                  acfg_result_t *out_result)
{
    if (handle == NULL || mono_samples == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sample_count != ACFG_WINDOW_SAMPLES) {
        return ESP_ERR_INVALID_SIZE;
    }

    float *tmp = handle->pcm_f32;
    if (tmp == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        tmp[i] = (float)mono_samples[i] / 32768.0f;
    }

    return acfg_predict_window_f32(handle, tmp, sample_count, out_result);
}

esp_err_t acfg_predict_sliding_f32(acfg_handle_t *handle,
                                   const float *mono_samples,
                                   size_t sample_count,
                                   acfg_window_result_cb_t cb,
                                   void *cb_ctx)
{
    if (handle == NULL || mono_samples == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t start = 0; start + ACFG_WINDOW_SAMPLES <= sample_count; start += ACFG_HOP_SAMPLES) {
        acfg_result_t result;
        esp_err_t err = acfg_predict_window_f32(handle,
                                                &mono_samples[start],
                                                ACFG_WINDOW_SAMPLES,
                                                &result);
        if (err != ESP_OK) {
            return err;
        }

        result.start_sample = (int)start;
        result.end_sample = (int)(start + ACFG_WINDOW_SAMPLES);
        result.start_time_s = (float)start / (float)ACFG_SAMPLE_RATE;
        result.end_time_s = (float)(start + ACFG_WINDOW_SAMPLES) / (float)ACFG_SAMPLE_RATE;

        cb(&result, cb_ctx);
    }

    return ESP_OK;
}
