#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t ap3216cDriver_init(i2c_master_bus_handle_t bus);
uint16_t ap3216cDriver_readALS(void);
uint16_t ap3216cDriver_readPS(void);
uint16_t ap3216cDriver_readIR(void);
