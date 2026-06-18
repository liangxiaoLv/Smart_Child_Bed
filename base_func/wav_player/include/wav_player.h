#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wavPlayer_init(i2c_master_bus_handle_t bus);
esp_err_t wavPlayer_play(const uint8_t *data, size_t len);
esp_err_t wavPlayer_stop(void);
esp_err_t wavPlayer_setVolume(uint8_t pct);
bool wavPlayer_isPlaying(void);

/** @brief 播放测试正弦波（绕过 WAV 解析，直接生成 PCM）
 *  @param freq_hz     频率 (Hz)，如 440
 *  @param duration_ms 持续时间 (ms)
 *  @param sample_rate 采样率 (Hz)，如 44100。传 0 则用当前 I2S 配置
 *  @return ESP_OK / 错误码
 */
esp_err_t wavPlayer_testTone(uint32_t freq_hz, uint32_t duration_ms, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif
