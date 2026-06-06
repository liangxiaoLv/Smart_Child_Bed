#pragma once
#include "esp_err.h"

/** 启动云端数据上报任务（MQTT 已连接后调用） */
esp_err_t trans2cloud_start(void);

/** 各传感器模块调用此接口更新最新数据，trans_2_cloud 定时打包上报 */
void trans2cloud_updateEnv(float temp_c, float hum_pct);
void trans2cloud_updateAir(uint8_t aqi, uint16_t tvoc_ppb, uint16_t eco2_ppm);
void trans2cloud_updateRadar(bool presence, uint8_t breath, uint8_t heart, bool move);
void trans2cloud_updateRadarFull(uint8_t person, uint8_t breath,
                                 uint8_t heart, uint8_t motion,
                                 uint8_t mod_status);
void trans2cloud_updateBcg(uint8_t person, uint8_t breath,
                           uint8_t heart, uint8_t move);
void trans2cloud_updateSleepRecord(bool recording);
void trans2cloud_updateBodyTemp(float temp_c);
/** 日期时间（年月日时分） */
typedef struct {
    uint16_t y;
    uint8_t  m, d, h, min;
} datetime_t;

/** 睡眠报告数据结构 */
typedef struct {
    datetime_t bed;      /* 上床时间 */
    datetime_t up;       /* 下床时间 */
    datetime_t sleep;    /* 入睡时间 */
    datetime_t wake;     /* 醒来时间 */
    uint16_t bed_mins;
    uint16_t sleep_mins;
    uint16_t awake_mins;
    uint16_t move_cnt;
} sleep_report_t;

void trans2cloud_updateSleepReport(const sleep_report_t *report);
void trans2cloud_updateSleepReportEmpty(void);
void trans2cloud_updateWifiSSID(const char *ssid);
