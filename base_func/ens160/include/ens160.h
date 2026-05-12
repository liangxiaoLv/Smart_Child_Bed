#pragma once

#include "esp_err.h"

esp_err_t ens160_info(void);
esp_err_t ens160_set_temp(float temp_c);
esp_err_t ens160_set_rh(float rh_pct);
