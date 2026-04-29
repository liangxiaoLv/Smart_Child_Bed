#ifndef _GTPIM_H_
#define _GTPIM_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "driver/gptimer.h"

typedef struct
{
    int timer_group;         // 定时器组号（0或1）
    int timer_idx;            // 定时器编号（0或1）
    uint64_t timing_time;     // 每次触发的时间间隔（微秒）
    uint64_t alarm_value;      // 下一次闹钟值
    bool auto_reload;          // 是否自动重载
    uint64_t timer_counter_value;  // 当前计数值
} timg_config_t;

void timg_init(timg_config_t *timg_config);
#endif