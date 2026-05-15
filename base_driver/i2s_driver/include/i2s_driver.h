#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint8_t  bits_per_sample;
    bool     stereo;
    int      bclk_io;
    int      ws_io;
    int      dout_io;
    int      din_io;
    int      mclk_io;
} i2sDriver_config_t;

#define I2S_DRIVER_DEFAULT_CONFIG() { \
    .sample_rate = 44100, \
    .bits_per_sample = 16, \
    .stereo = true, \
    .din_io = -1, \
    .mclk_io = -1, \
}

esp_err_t i2sDriver_init(const i2sDriver_config_t *config);
esp_err_t i2sDriver_write(const uint8_t *data, size_t bytes, size_t *written, uint32_t timeout_ms);
esp_err_t i2sDriver_read(uint8_t *buf, size_t bytes, size_t *read, uint32_t timeout_ms);
esp_err_t i2sDriver_deinit(void);
bool i2sDriver_isInited(void);

#ifdef __cplusplus
}
#endif
