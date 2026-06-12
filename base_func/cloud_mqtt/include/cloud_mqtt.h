#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#define MQTT_QOS_AT_MOST_ONCE      0
#define MQTT_QOS_AT_LEAST_ONCE     1
#define MQTT_QOS_EXACTLY_ONCE      2

/** 启动 MQTT 客户端（WiFi 已连接后调用） */
esp_err_t mqttClient_start(void);

/** 发布消息到指定话题 */
esp_err_t mqttClient_publish(const char *topic, const char *payload);

/** 发布二进制数据到指定话题 (支持音频等非文本数据) */
esp_err_t mqttClient_publishBinary(const char *topic, const uint8_t *data, size_t len);

/** 注册收到控制指令时的回调 */
void mqttClient_onCommand(void (*cb)(const char *topic, const char *payload));

/** 注册音频开始回调: name=文件名, total_size=预期总字节数 */
void mqttClient_onAudioStart(void (*cb)(const char *name, size_t total_size));

/** 注册音频数据块回调: data=原始二进制, len=字节数 */
void mqttClient_onAudioChunk(void (*cb)(const uint8_t *data, size_t len));
