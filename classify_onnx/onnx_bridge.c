#include "onnx_bridge.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "onnx_bridge";

extern const unsigned char _binary_best_model_onnx_start[] asm("_binary_best_model_onnx_start");
extern const unsigned char _binary_best_model_onnx_end[] asm("_binary_best_model_onnx_end");

typedef struct {
    const unsigned char *model_data;
    size_t model_size;
    bool ready;
} onnx_audio_context_t;

static onnx_audio_context_t s_ctx;

esp_err_t onnx_audio_model_init(void)
{
    if (s_ctx.ready) {
        return ESP_OK;
    }

    s_ctx.model_data = _binary_best_model_onnx_start;
    s_ctx.model_size = (size_t)(_binary_best_model_onnx_end - _binary_best_model_onnx_start);
    if (!s_ctx.model_data || s_ctx.model_size == 0) {
        ESP_LOGE(TAG, "embedded best_model.onnx is empty");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG,
             "best_model.onnx embedded (%u bytes), but no ONNX runtime is linked for ESP32-S3",
             (unsigned)s_ctx.model_size);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t onnx_audio_model_invoke(const float *input,
                                  size_t input_len,
                                  float *logits,
                                  size_t logits_len,
                                  void *user_ctx)
{
    (void)input;
    (void)input_len;
    (void)logits;
    (void)logits_len;
    (void)user_ctx;

    ESP_LOGE(TAG, "ONNX inference is not available: add an ESP-IDF ONNX runtime or generated C backend");
    return ESP_ERR_NOT_SUPPORTED;
}

void *onnx_audio_model_ctx(void)
{
    return &s_ctx;
}
