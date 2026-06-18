#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief AW88399QNR 音频功放初始化（DSP 旁路模式）
 *
 * 挂载到对应 I2C 总线，校验 CHIPID (0x32)，配置 I2S 输入格式和初始音量（80%）。
 * 寄存器为 8-bit 访问，地址映射参考官方 aw883xx PID_2183 寄存器表。
 * 本驱动使用 DSP 旁路模式（I2S → DAC 直通），不加载固件。
 *
 * @param bus  I2C 总线句柄
 * @return ESP_OK / 错误码
 */
esp_err_t aw88399qnr_init(i2c_master_bus_handle_t bus);

/** @brief 掉电并释放设备 */
esp_err_t aw88399qnr_deinit(void);

/** @brief 设置音量
 * @param pct  0（静音）~ 100（最大）
 */
esp_err_t aw88399qnr_setVolume(uint8_t pct);

/** @brief 是否已初始化 */
bool aw88399qnr_isInited(void);

/** @brief 同步 I2S 采样率到 AW88399QNR
 *
 * 当 ESP32 I2S 输出采样率改变时调用，更新芯片内部 PLL 分频。
 * @param sample_rate  I2S 采样率 (Hz)，支持 8000/11025/12000/16000/
 *                     22050/24000/32000/44100/48000
 * @return ESP_OK / ESP_ERR_NOT_SUPPORTED
 */
esp_err_t aw88399qnr_setSampleRate(uint32_t sample_rate);

/** @brief I2C 寄存器读写诊断（8-bit 模式 + 2字节连续读） */
void aw88399qnr_testWrites(void);

#ifdef __cplusplus
}
#endif
