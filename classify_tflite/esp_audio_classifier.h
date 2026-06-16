#ifndef ESP_AUDIO_CLASSIFIER_H
#define ESP_AUDIO_CLASSIFIER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACFG_SAMPLE_RATE        44100
#define ACFG_WINDOW_SECONDS     1
#define ACFG_WINDOW_SAMPLES     (ACFG_SAMPLE_RATE * ACFG_WINDOW_SECONDS)
#define ACFG_HOP_SAMPLES        11025
#define ACFG_N_FFT              2048
#define ACFG_FFT_BINS           (ACFG_N_FFT / 2 + 1)
#define ACFG_SPEC_HOP_LENGTH    512
#define ACFG_N_MELS             64
#define ACFG_N_FRAMES           87
#define ACFG_FEATURE_COUNT      (ACFG_N_MELS * ACFG_N_FRAMES)
#define ACFG_CLASS_COUNT        3

typedef enum {
    ACFG_CLASS_OTHER = 0,
    ACFG_CLASS_COUGH = 1,
    ACFG_CLASS_CRYING = 2,
} acfg_class_t;

typedef struct {
    int start_sample;
    int end_sample;
    float start_time_s;
    float end_time_s;
    int pred_class;
    float probability;
    float probs[ACFG_CLASS_COUNT];
    float db;
} acfg_result_t;

typedef esp_err_t (*acfg_invoke_fn_t)(const float *input,
                                      size_t input_len,
                                      float *logits,
                                      size_t logits_len,
                                      void *user_ctx);

typedef void (*acfg_window_result_cb_t)(const acfg_result_t *result,
                                        void *user_ctx);

typedef struct {
    acfg_invoke_fn_t invoke;
    void *invoke_ctx;

    float *hann_window;
    float *mel_fbanks;
    float *fft_buf;
    float *power_spec;
    float *features;
    float *pcm_f32;
} acfg_handle_t;

const char *acfg_class_name(int class_id);

esp_err_t acfg_init(acfg_handle_t *handle,
                    acfg_invoke_fn_t invoke,
                    void *invoke_ctx);

void acfg_deinit(acfg_handle_t *handle);

esp_err_t acfg_preprocess_f32(acfg_handle_t *handle,
                              const float *mono_samples,
                              size_t sample_count,
                              float *out_features,
                              size_t out_feature_count);

esp_err_t acfg_predict_window_f32(acfg_handle_t *handle,
                                  const float *mono_samples,
                                  size_t sample_count,
                                  acfg_result_t *out_result);

esp_err_t acfg_predict_window_i16(acfg_handle_t *handle,
                                  const int16_t *mono_samples,
                                  size_t sample_count,
                                  acfg_result_t *out_result);

esp_err_t acfg_predict_sliding_f32(acfg_handle_t *handle,
                                   const float *mono_samples,
                                   size_t sample_count,
                                   acfg_window_result_cb_t cb,
                                   void *cb_ctx);


extern esp_err_t tflm_audio_model_init(void);
extern esp_err_t tflm_audio_model_invoke(const float *input,
                                        size_t input_len,
                                        float *logits,
                                        size_t logits_len,
                                        void *user_ctx);
extern void *tflm_audio_model_ctx(void);

#ifdef __cplusplus
}
#endif

#endif
