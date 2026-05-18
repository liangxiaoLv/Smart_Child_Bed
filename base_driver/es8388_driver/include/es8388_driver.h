#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t es8388Driver_init(void);
esp_err_t es8388Driver_deinit(void);
esp_err_t es8388Driver_setVolume(uint8_t vol);   /* 0~33 */
esp_err_t es8388Driver_setHPVolume(uint8_t vol);  /* 0~33 */

#ifdef __cplusplus
}
#endif
