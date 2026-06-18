#pragma once

#include "driver/i2s_std.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct i2sDriver_t *i2sDriver_handle_t;

typedef struct {
    uint32_t   sample_rate;
    uint8_t    bits_per_sample;
    bool       stereo;
    bool       enable_tx;
    bool       enable_rx;
    int        mclk_io;
    int        bclk_io;
    int        ws_io;
    int        dout_io;
    int        din_io;
    uint32_t   mclk_multiple;   /* MCLK/FS 倍率, 默认 256, 24-bit 须为 3 的倍数 */
} i2sDriver_config_t;

#define I2S_DRIVER_DEFAULT_CONFIG() { \
    .sample_rate = 44100, \
    .bits_per_sample = 16, \
    .stereo = true, \
    .enable_tx = true, \
    .enable_rx = false, \
    .din_io = -1, \
    .mclk_io = -1, \
    .mclk_multiple = 256, \
}

/* 初始化 I2S 通道，port 取值 I2S_NUM_0 / I2S_NUM_AUTO 等。
   必须在系统启动阶段单线程调用，不可在多个任务中并发调用。 */
esp_err_t i2sDriver_init(int port, const i2sDriver_config_t *config, i2sDriver_handle_t *handle_out);
esp_err_t i2sDriver_write(i2sDriver_handle_t handle, const uint8_t *data, size_t bytes, size_t *written, uint32_t timeout_ms);
esp_err_t i2sDriver_read(i2sDriver_handle_t handle, uint8_t *buf, size_t bytes, size_t *read, uint32_t timeout_ms);
esp_err_t i2sDriver_deinit(i2sDriver_handle_t handle);
/* 仅重配时钟，不重建通道（避免每次播放 deinit/reinit）*/
esp_err_t i2sDriver_reconfigClock(i2sDriver_handle_t handle, uint32_t sample_rate, uint8_t bits_per_sample, bool stereo);

#ifdef __cplusplus
}
#endif
