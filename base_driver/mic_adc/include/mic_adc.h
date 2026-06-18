#pragma once

#include "esp_err.h"
#include "es7210_adc.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *mic_adc_handle_t;

typedef struct {
    void   *i2c_bus;        /* I2C 总线句柄 (i2c_master_bus_handle_t) */
    uint8_t                 mic_mask;  /* 麦克风通道掩码, ES7210_SEL_MIC1|MIC2|MIC3 */
    uint32_t                sample_rate; /* 采样率 Hz (16000) */
    uint8_t                 channel_num; /* 通道数 (2 或 3) */
    int                     mclk_io;   /* MCLK 引脚 */
    int                     bclk_io;   /* BCLK 引脚 */
    int                     ws_io;     /* WS 引脚 */
    int                     din_io;    /* DIN 引脚 */
} mic_adc_config_t;

/**
 * 初始化麦克风 ADC (ES7210 + I2S)
 *
 * 通过 esp_codec_dev 官方库配置 ES7210 并启动 I2S 接收。
 * 调用成功后即可通过 mic_adc_read() 读取音频数据。
 *
 * @param cfg   配置参数
 * @param handle 输出句柄
 */
esp_err_t mic_adc_init(const mic_adc_config_t *cfg, mic_adc_handle_t *handle);

/**
 * 读取音频采样数据
 *
 * @param handle  句柄
 * @param buf     输出缓冲区 (int16_t 交错立体声)
 * @param samples 期望读取的采样点数 (每通道)
 * @return        实际读取的采样点数, <0 表示错误
 */
int mic_adc_read(mic_adc_handle_t handle, int16_t *buf, int samples);

/**
 * 板级 ES7210 寄存器补丁 (esp_codec_dev 默认 REG4B=0x00 在本板会静音)
 * 须在 mic_adc_init() 成功后调用。
 */
esp_err_t mic_adc_apply_board_patch(mic_adc_handle_t handle);

/**
 * 关闭并释放麦克风 ADC
 */
esp_err_t mic_adc_deinit(mic_adc_handle_t handle);

#ifdef __cplusplus
}
#endif
