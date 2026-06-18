#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/** @brief 初始化旋转编码器（A/B 相 + 按键） */
esp_err_t ROTARY_ENCODER_GET(void);

/** @brief 获取累计脉冲数（每次读取后清零） */
int32_t rotaryEncoder_getDelta(void);

/** @brief 获取按键边沿（按下→释放 或 释放→按下），每次返回后清零 */
bool rotaryEncoder_buttonEdge(void);
