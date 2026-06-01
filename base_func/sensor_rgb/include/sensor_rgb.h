#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * 启动环境光→RGB 灯带自动控制。
 *
 * 逻辑：环境光亮 → 灯带暗  /  环境光暗 → 灯带亮
 *
 * @param bus  AP3216C 所在的 I2C 总线句柄（I2C0）
 * @return     ESP_OK 成功，否则失败
 *
 * 调用前需确保 rgbLed_work() 已初始化。
 */
esp_err_t SENSOR_CONTROL_RGB(i2c_master_bus_handle_t bus);

/** 开启/关闭自动控制（关闭后恢复手动亮度） */
esp_err_t sensorCtrlRgb_enable(bool en);

/** 获取当前环境光 raw 值（16-bit ADC，仅供参考） */
uint16_t sensorCtrlRgb_getAls(void);
