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

/**
 * 启动麦克风采样 + 上传 + 持续分类
 *
 * - 前 10 秒与 micSample_start 相同: 采样并上传 WAV 到服务器
 * - 10 秒后进入持续分类模式:
 *   以 44100Hz 立体声采样, 转单声道后每秒用 TFLite 模型推理一次,
 *   分类结果(类别/概率/分贝)通过 ESP_LOGI 输出到日志
 *
 * @param bus  I2C 总线句柄 (i2c_master_bus_handle_t)
 */
esp_err_t micSample_start_classify(void *bus);
