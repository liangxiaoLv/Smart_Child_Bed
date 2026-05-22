#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t ens160_info(i2c_master_bus_handle_t bus);
esp_err_t ens160_set_temp(float temp_c);
esp_err_t ens160_set_rh(float rh_pct);

/** 获取最新 AQI 值（由采集任务更新） */
uint8_t ens160_getLatestAQI(void);
