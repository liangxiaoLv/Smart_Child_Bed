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

#ifdef __cplusplus
}
#endif
