#pragma once

#include "esp_err.h"

esp_err_t tfCardDriver_init(void);
const char *tfCardDriver_getMountPoint(void);
esp_err_t tfCardDriver_deinit(void);
