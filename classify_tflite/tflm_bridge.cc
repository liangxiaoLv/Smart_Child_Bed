#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <new>

#include "esp_audio_classifier.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define TFLM_AUDIO_TAG "tflm_audio"

#ifndef TFLM_AUDIO_TENSOR_ARENA_SIZE
#define TFLM_AUDIO_TENSOR_ARENA_SIZE (1536 * 1024)
#endif

extern const unsigned char _binary_best_model_tflite_start[] asm("_binary_best_model_tflite_start");
extern const unsigned char _binary_best_model_tflite_end[] asm("_binary_best_model_tflite_end");

namespace {

struct TflmAudioContext {
    const tflite::Model *model;
    tflite::MicroMutableOpResolver<32> *resolver;
    tflite::MicroInterpreter *interpreter;
    uint8_t *tensor_arena;
    size_t tensor_arena_size;
    TfLiteTensor *input;
    TfLiteTensor *output;
    bool ready;
};

TflmAudioContext g_ctx = {};

void reset_context(void)
{
    delete g_ctx.interpreter;
    delete g_ctx.resolver;

    if (g_ctx.tensor_arena != nullptr) {
        heap_caps_free(g_ctx.tensor_arena);
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
}

void *arena_alloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr != nullptr) {
        return ptr;
    }

    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

TfLiteStatus register_model_ops(tflite::MicroMutableOpResolver<32> *resolver)
{
    if (resolver == nullptr) {
        return kTfLiteError;
    }

    TfLiteStatus status = kTfLiteOk;
    status = resolver->AddAdd();
    if (status != kTfLiteOk) return status;
    status = resolver->AddAveragePool2D();
    if (status != kTfLiteOk) return status;
    status = resolver->AddConcatenation();
    if (status != kTfLiteOk) return status;
    status = resolver->AddConv2D();
    if (status != kTfLiteOk) return status;
    status = resolver->AddDepthwiseConv2D();
    if (status != kTfLiteOk) return status;
    status = resolver->AddDequantize();
    if (status != kTfLiteOk) return status;
    status = resolver->AddDiv();
    if (status != kTfLiteOk) return status;
    status = resolver->AddExpandDims();
    if (status != kTfLiteOk) return status;
    status = resolver->AddFullyConnected();
    if (status != kTfLiteOk) return status;
    status = resolver->AddGather();
    if (status != kTfLiteOk) return status;
    status = resolver->AddHardSwish();
    if (status != kTfLiteOk) return status;
    status = resolver->AddLogistic();
    if (status != kTfLiteOk) return status;
    status = resolver->AddMaximum();
    if (status != kTfLiteOk) return status;
    status = resolver->AddMaxPool2D();
    if (status != kTfLiteOk) return status;
    status = resolver->AddMean();
    if (status != kTfLiteOk) return status;
    status = resolver->AddMinimum();
    if (status != kTfLiteOk) return status;
    status = resolver->AddMul();
    if (status != kTfLiteOk) return status;
    status = resolver->AddPad();
    if (status != kTfLiteOk) return status;
    status = resolver->AddPadV2();
    if (status != kTfLiteOk) return status;
    status = resolver->AddPack();
    if (status != kTfLiteOk) return status;
    status = resolver->AddQuantize();
    if (status != kTfLiteOk) return status;
    status = resolver->AddRelu();
    if (status != kTfLiteOk) return status;
    status = resolver->AddRelu6();
    if (status != kTfLiteOk) return status;
    status = resolver->AddReshape();
    if (status != kTfLiteOk) return status;
    status = resolver->AddShape();
    if (status != kTfLiteOk) return status;
    status = resolver->AddSlice();
    if (status != kTfLiteOk) return status;
    status = resolver->AddSoftmax();
    if (status != kTfLiteOk) return status;
    status = resolver->AddStridedSlice();
    if (status != kTfLiteOk) return status;
    status = resolver->AddSub();
    if (status != kTfLiteOk) return status;
    status = resolver->AddSqueeze();
    if (status != kTfLiteOk) return status;
    status = resolver->AddSum();
    if (status != kTfLiteOk) return status;
    status = resolver->AddTranspose();
    return status;
}

size_t tensor_element_count(const TfLiteTensor *tensor)
{
    if (tensor == nullptr || tensor->dims == nullptr || tensor->dims->size <= 0) {
        return 0;
    }

    size_t count = 1;
    for (int i = 0; i < tensor->dims->size; ++i) {
        count *= (size_t)tensor->dims->data[i];
    }
    return count;
}

float dequantize_int8(int8_t value, const TfLiteQuantizationParams &params)
{
    return ((int)value - params.zero_point) * params.scale;
}

float dequantize_uint8(uint8_t value, const TfLiteQuantizationParams &params)
{
    return ((int)value - params.zero_point) * params.scale;
}

int8_t quantize_int8(float value, const TfLiteQuantizationParams &params)
{
    if (params.scale == 0.0f) {
        return 0;
    }

    int q = (int)lrintf(value / params.scale) + params.zero_point;
    if (q < -128) {
        q = -128;
    } else if (q > 127) {
        q = 127;
    }
    return (int8_t)q;
}

uint8_t quantize_uint8(float value, const TfLiteQuantizationParams &params)
{
    if (params.scale == 0.0f) {
        return 0;
    }

    int q = (int)lrintf(value / params.scale) + params.zero_point;
    if (q < 0) {
        q = 0;
    } else if (q > 255) {
        q = 255;
    }
    return (uint8_t)q;
}

esp_err_t copy_input_features(const float *input, size_t input_len, TfLiteTensor *tensor)
{
    size_t tensor_len = tensor_element_count(tensor);
    if (tensor_len != input_len) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "input size mismatch: model=%u, caller=%u",
                 (unsigned)tensor_len,
                 (unsigned)input_len);
        return ESP_ERR_INVALID_SIZE;
    }

    switch (tensor->type) {
    case kTfLiteFloat32:
        memcpy(tensor->data.f, input, input_len * sizeof(float));
        return ESP_OK;

    case kTfLiteInt8:
        for (size_t i = 0; i < input_len; ++i) {
            tensor->data.int8[i] = quantize_int8(input[i], tensor->params);
        }
        return ESP_OK;

    case kTfLiteUInt8:
        for (size_t i = 0; i < input_len; ++i) {
            tensor->data.uint8[i] = quantize_uint8(input[i], tensor->params);
        }
        return ESP_OK;

    default:
        ESP_LOGE(TFLM_AUDIO_TAG, "unsupported input tensor type: %d", tensor->type);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t copy_output_logits(const TfLiteTensor *tensor, float *logits, size_t logits_len)
{
    size_t tensor_len = tensor_element_count(tensor);
    if (tensor_len != logits_len) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "output size mismatch: model=%u, caller=%u",
                 (unsigned)tensor_len,
                 (unsigned)logits_len);
        return ESP_ERR_INVALID_SIZE;
    }

    switch (tensor->type) {
    case kTfLiteFloat32:
        memcpy(logits, tensor->data.f, logits_len * sizeof(float));
        return ESP_OK;

    case kTfLiteInt8:
        for (size_t i = 0; i < logits_len; ++i) {
            logits[i] = dequantize_int8(tensor->data.int8[i], tensor->params);
        }
        return ESP_OK;

    case kTfLiteUInt8:
        for (size_t i = 0; i < logits_len; ++i) {
            logits[i] = dequantize_uint8(tensor->data.uint8[i], tensor->params);
        }
        return ESP_OK;

    default:
        ESP_LOGE(TFLM_AUDIO_TAG, "unsupported output tensor type: %d", tensor->type);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

}  // namespace

extern "C" esp_err_t tflm_audio_model_init(void)
{
    if (g_ctx.ready) {
        return ESP_OK;
    }
    if (g_ctx.interpreter != nullptr) {
        ESP_LOGE(TFLM_AUDIO_TAG, "model init previously failed; reboot before retrying");
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t *model_data = _binary_best_model_tflite_start;
    size_t model_size = (size_t)(_binary_best_model_tflite_end - _binary_best_model_tflite_start);
    if (model_data == nullptr || model_size == 0) {
        ESP_LOGE(TFLM_AUDIO_TAG, "embedded best_model.tflite is empty");
        return ESP_FAIL;
    }

    g_ctx.model = tflite::GetModel(model_data);
    if (g_ctx.model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "model schema=%d, runtime schema=%d",
                 g_ctx.model->version(),
                 TFLITE_SCHEMA_VERSION);
        return ESP_ERR_NOT_SUPPORTED;
    }

    g_ctx.tensor_arena_size = TFLM_AUDIO_TENSOR_ARENA_SIZE;
    g_ctx.tensor_arena = (uint8_t *)arena_alloc(g_ctx.tensor_arena_size);
    if (g_ctx.tensor_arena == nullptr) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "failed to allocate tensor arena: %u bytes",
                 (unsigned)g_ctx.tensor_arena_size);
        return ESP_ERR_NO_MEM;
    }

    g_ctx.resolver = new (std::nothrow) tflite::MicroMutableOpResolver<32>();
    if (g_ctx.resolver == nullptr) {
        ESP_LOGE(TFLM_AUDIO_TAG, "failed to allocate op resolver");
        reset_context();
        return ESP_ERR_NO_MEM;
    }

    if (register_model_ops(g_ctx.resolver) != kTfLiteOk) {
        ESP_LOGE(TFLM_AUDIO_TAG, "failed to register TFLite Micro ops");
        reset_context();
        return ESP_FAIL;
    }

    g_ctx.interpreter = new (std::nothrow) tflite::MicroInterpreter(g_ctx.model,
                                                                    *g_ctx.resolver,
                                                                    g_ctx.tensor_arena,
                                                                    g_ctx.tensor_arena_size);
    if (g_ctx.interpreter == nullptr) {
        ESP_LOGE(TFLM_AUDIO_TAG, "failed to allocate interpreter");
        reset_context();
        return ESP_ERR_NO_MEM;
    }

    TfLiteStatus status = g_ctx.interpreter->AllocateTensors();
    if (status != kTfLiteOk) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "AllocateTensors failed, check missing op log or increase TFLM_AUDIO_TENSOR_ARENA_SIZE");
        return ESP_FAIL;
    }

    g_ctx.input = g_ctx.interpreter->input(0);
    g_ctx.output = g_ctx.interpreter->output(0);

    size_t input_count = tensor_element_count(g_ctx.input);
    size_t output_count = tensor_element_count(g_ctx.output);
    if (input_count != ACFG_FEATURE_COUNT || output_count != ACFG_CLASS_COUNT) {
        ESP_LOGE(TFLM_AUDIO_TAG,
                 "unexpected model shape: input=%u expected=%u, output=%u expected=%u",
                 (unsigned)input_count,
                 (unsigned)ACFG_FEATURE_COUNT,
                 (unsigned)output_count,
                 (unsigned)ACFG_CLASS_COUNT);
        reset_context();
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TFLM_AUDIO_TAG,
             "model loaded: %u bytes, arena=%u, input_type=%d, output_type=%d",
             (unsigned)model_size,
             (unsigned)g_ctx.tensor_arena_size,
             g_ctx.input->type,
             g_ctx.output->type);

    g_ctx.ready = true;
    return ESP_OK;
}

extern "C" esp_err_t tflm_audio_model_invoke(const float *input,
                                             size_t input_len,
                                             float *logits,
                                             size_t logits_len,
                                             void *user_ctx)
{
    TflmAudioContext *ctx = user_ctx != nullptr ? (TflmAudioContext *)user_ctx : &g_ctx;

    if (ctx == nullptr || !ctx->ready || ctx->interpreter == nullptr ||
        ctx->input == nullptr || ctx->output == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (input == nullptr || logits == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = copy_input_features(input, input_len, ctx->input);
    if (err != ESP_OK) {
        return err;
    }

    TfLiteStatus status = ctx->interpreter->Invoke();
    if (status != kTfLiteOk) {
        ESP_LOGE(TFLM_AUDIO_TAG, "Invoke failed");
        return ESP_FAIL;
    }

    return copy_output_logits(ctx->output, logits, logits_len);
}

extern "C" void *tflm_audio_model_ctx(void)
{
    return &g_ctx;
}
