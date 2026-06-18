#pragma once

#include "esp_err.h"

/**
 * 启动麦克风采样测试并上传 (10 秒)
 *
 * - 初始化 I2S0 + ES7210
 * - 采样 10 秒, 每秒打印一次数据到日志
 * - 转为 16-bit 单声道 WAV, 通过 MQTT 分块上传到服务器
 *
 * @param bus  I2C 总线句柄 (i2c_master_bus_handle_t)
 */
esp_err_t micSample_start(void *bus);
