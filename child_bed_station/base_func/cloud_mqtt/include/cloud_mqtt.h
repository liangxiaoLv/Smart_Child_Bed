#pragma once
#include "esp_err.h"

/** 启动 MQTT 客户端（WiFi 已连接后调用） */
esp_err_t mqttClient_start(void);

/** 发布消息到指定话题 */
esp_err_t mqttClient_publish(const char *topic, const char *payload);

/** 注册收到控制指令时的回调 */
void mqttClient_onCommand(void (*cb)(const char *topic, const char *payload));
