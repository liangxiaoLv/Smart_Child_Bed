#ifndef CLASSIFY_TFLITE_SERVICE_H
#define CLASSIFY_TFLITE_SERVICE_H

#include "esp_err.h"
#include "esp_audio_classifier.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t classifyTflite_start(void);

/**
 * 对 1 秒单声道 int16 音频窗口做分类推理
 *
 * @param mono_samples  单声道 int16 PCM 数据, 长度必须为 ACFG_WINDOW_SAMPLES (44100)
 * @param sample_count  样本数, 必须 == ACFG_WINDOW_SAMPLES
 * @param out_result    输出分类结果
 * @return ESP_OK 成功, 其他值失败
 */
esp_err_t classifyTflite_predict_i16(const int16_t *mono_samples,
                                     size_t sample_count,
                                     acfg_result_t *out_result);

#ifdef __cplusplus
}
#endif

#endif
