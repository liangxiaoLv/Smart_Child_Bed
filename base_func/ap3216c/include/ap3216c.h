#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

/** 初始化 AP3216C，挂载到指定 I2C 总线并启动采集任务 */
esp_err_t ap3216c_init(i2c_master_bus_handle_t bus);

/** 获取最近一次环境光 ALS 原始值（16-bit ADC） */
uint16_t ap3216c_getAls(void);
