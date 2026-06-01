#pragma once
#include "esp_err.h"

/** 初始化蜂鸣器 GPIO，启动蜂鸣控制任务 */
esp_err_t buzzer_init(void);

/** 开启/关闭蜂鸣器报警 */
void buzzer_setWarning(bool on);
