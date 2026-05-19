#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t ens210_temp_info(i2c_master_bus_handle_t bus);
