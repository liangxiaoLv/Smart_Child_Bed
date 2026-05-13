#pragma once
#include "esp_err.h"

/** 启动云端数据上报任务（MQTT 已连接后调用） */
esp_err_t trans2cloud_start(void);

/** 各传感器模块调用此接口更新最新数据，trans_2_cloud 定时打包上报 */
void trans2cloud_updateEnv(float temp_c, float hum_pct);
void trans2cloud_updateAir(uint8_t aqi, uint16_t tvoc_ppb, uint16_t eco2_ppm);
void trans2cloud_updateRadar(bool presence, uint8_t breath, uint8_t heart, bool move);
