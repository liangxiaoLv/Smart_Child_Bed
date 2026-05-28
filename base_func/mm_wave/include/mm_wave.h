#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/* 启动雷达数据接收任务 */
esp_err_t mm_wave_radar_info(void);

/* 发送原始数据（不受5秒间隔限制，用于协议自动应答） */
esp_err_t mmWave_send(const uint8_t *data, size_t len);

/* 便捷命令接口（受5秒间隔保护） */
esp_err_t mmWave_queryVersion(void);       /* 查询软硬件版本 */
esp_err_t mmWave_queryDeviceId(void);      /* 查询设备ID */
esp_err_t mmWave_startSleep(void);         /* 开始睡眠监测 */
esp_err_t mmWave_endSleep(void);           /* 结束睡眠记录 */
esp_err_t mmWave_querySleepReport(void);   /* 查询睡眠报告 */
esp_err_t mmWave_setTime(uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute);  /* 设置绝对时间 */
