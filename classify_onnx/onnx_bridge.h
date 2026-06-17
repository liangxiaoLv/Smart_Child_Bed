#ifndef ONNX_BRIDGE_H
#define ONNX_BRIDGE_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t onnx_audio_model_init(void);

esp_err_t onnx_audio_model_invoke(const float *input,
                                  size_t input_len,
                                  float *logits,
                                  size_t logits_len,
                                  void *user_ctx);

void *onnx_audio_model_ctx(void);

#ifdef __cplusplus
}
#endif

#endif
