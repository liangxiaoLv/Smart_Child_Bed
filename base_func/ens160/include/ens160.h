#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t ens160_info(i2c_master_bus_handle_t bus);
esp_err_t ens160_set_temp(float temp_c);
esp_err_t ens160_set_rh(float rh_pct);
