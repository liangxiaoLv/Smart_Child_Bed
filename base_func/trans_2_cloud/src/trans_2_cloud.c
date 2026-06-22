#include "trans_2_cloud.h"
#include "cloud_mqtt.h"
#include "rgb_led.h"
#include "wav_player.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
/* mmWave 接口（extern 声明避免循环依赖）
 * 当前未启用，模块启用后取消 #if 0 */
#if 0
extern esp_err_t mmWave_startSleep(void);
extern esp_err_t mmWave_endSleep(void);
extern esp_err_t mmWave_querySleepReport(void);
#endif

// 数据上报间隔（5秒）
#define PUBLISH_INTERVAL_MS  5000

// mqtt心跳监测间隔（60秒）
#define HEARTBEAT_INTERVAL_MS 60000

static const char *TAG = "trans_cloud";

/* ─── 最新传感器数据（各传感器模块更新写入）───────────────── */

// 环境温湿度 ens210传感器
static float  s_env_temp  = 0;   // 环境温度
static float  s_env_hum   = 0;   // 环境湿度
// 空气质量 ens160传感器
static uint8_t  s_aqi     = 0;   // 空气质量指数
static uint16_t s_tvoc    = 0;   // 总挥发性有机化合物浓度
static uint16_t s_eco2    = 0;   // 二氧化碳浓度



/*------------BCG 体征数据------------*/
static uint8_t s_bcg_person   = 0; // 检测是否有人
static uint8_t s_radar_person = 0; 

static uint8_t s_bcg_move        = 0; // 0=不体动 1=小体动
static uint8_t s_radar_move      = 0; // 0=无体动 1=小体动 2=大体动

static uint8_t s_bcg_breath      = 0; // 呼吸频率（次/分钟）
static uint8_t s_radar_breath    = 0; 

static uint8_t s_bcg_heart       = 0;  // 心率（次/分钟）
static uint8_t s_radar_heart     = 0; 

static uint8_t s_radar_status    = 0;   /*毫米波监测状态 0=未监测 1=监测中 2=未监测(有报告) 3=等待时间 */

static bool    s_radar_sleep_recording  = false;

/*------------睡眠报告数据------------
BCG 睡眠监测报告字段
*/
static bool    s_sr_valid = false;
static char    s_sr_bed[24];       /* "YYYY-MM-DD HH:MM" */
static char    s_sr_up[24];
static char    s_sr_sleep[24];
static char    s_sr_wake[24];
static uint16_t s_sr_bed_mins;
static uint16_t s_sr_sleep_mins;
static uint16_t s_sr_awake_mins;
static uint16_t s_sr_move_cnt;



/*------------红外体温传感器------------*/
static float   s_body_temp = 0;
static TickType_t s_fever_last_report = 0;  /* 上次发烧预警发布时刻 */

static char    s_ssid[33] = "";



/* ─── 云端话题 ─────────────────────────────────────────────── */
#define TOPIC_STATUS       "bed/status"
#define TOPIC_HEARTBEAT    "bed/heartbeat"
#define TOPIC_SLEEP_REPORT "bed/sleep_report"
#define TOPIC_FEVER        "bed/fever"

/* 发烧判断阈值 */
#define FEVER_THRESHOLD_C  38.5f
/* 发烧预警冷却时间（毫秒），冷却期内不重复上报 */
#define FEVER_COOLDOWN_MS  90000

/* ─── 数据上报任务 ─────────────────────────────────────────── */
static void reportTask(void *arg)
{
    char json[512];
    for (;;) {
        snprintf(json, sizeof(json),
            "{\"ssid\":\"%s\","
            "\"env_temp\":%.1f,\"env_hum\":%.1f,"
            "\"aqi\":%d,\"tvoc\":%d,\"eco2\":%d,"
            "\"bcg_person\":%d,\"bcg_breath\":%d,\"bcg_heart\":%d,\"bcg_move\":%d,"
            "\"body_temp\":%.1f}",
            s_ssid,
            s_env_temp, s_env_hum,
            s_aqi, s_tvoc, s_eco2,
            s_bcg_person, s_bcg_breath, s_bcg_heart, s_bcg_move,
            s_body_temp);
    
        ESP_LOGI(TAG, "bcg_person=%d bcg_breath=%d bcg_heart=%d bcg_move=%d body_temp=%.1f",
                 s_bcg_person, s_bcg_breath, s_bcg_heart, s_bcg_move, (double)s_body_temp);

        mqttClient_publish(TOPIC_STATUS, json);
        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

/*------------MQTT心跳监测------------*/
static void heartbeatTask(void *arg)
{
    for (;;) {
        mqttClient_publish(TOPIC_HEARTBEAT, "\"online\"");
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}

/*------------温湿度数据更新接口------------*/
void trans2cloud_updateEnv(float temp_c, float hum_pct)
{
    s_env_temp = temp_c;
    s_env_hum  = hum_pct;
}

/*------------空气质量数据更新接口------------*/
void trans2cloud_updateAir(uint8_t aqi, uint16_t tvoc_ppb, uint16_t eco2_ppm)
{
    s_aqi  = aqi;
    s_tvoc = tvoc_ppb;
    s_eco2 = eco2_ppm;
}

/*------------毫米波雷达数据更新接口------------*/
void trans2cloud_updateRadar(bool presence, uint8_t breath, uint8_t heart, bool move)
{
    s_radar_person = presence;
    s_radar_breath   = breath;
    s_radar_heart    = heart;
    s_radar_move     = move;
}

#if 0
void trans2cloud_updateRadarFull(uint8_t person, uint8_t breath,
                                 uint8_t heart, uint8_t motion,
                                 uint8_t mod_status)
{
    s_radar_person = person;
    s_radar_breath = breath;
    s_radar_heart  = heart;
    s_radar_move   = motion;
    s_radar_status = mod_status;

    /* 同步兼容旧字段 */
    s_bcg_person = (person == 1);
    s_bcg_breath = breath;
    s_bcg_heart  = heart;
    s_bcg_move   = (motion != 0);

    /* 监测状态变化时更新睡眠记录标志 */
    s_radar_sleep_recording = (mod_status == 1);
}
#endif

/*------------BCG 体征数据更新接口------------*/
void trans2cloud_updateBcg(uint8_t person, uint8_t breath,
                           uint8_t heart, uint8_t move)
{
    s_bcg_person = person;
    s_bcg_breath = breath;
    s_bcg_heart  = heart;
    s_bcg_move   = move;
}

/*------------是否有睡眠报告------------*/
void trans2cloud_updateSleepRecord(bool recording)
{
    s_radar_sleep_recording = recording;
}

/*------------体温数据更新接口------------*/
void trans2cloud_updateBodyTemp(float temp_c)
{
    s_body_temp = temp_c;

    /* 体温超过阈值时立即上报发烧预警，冷却期内不重复上报 */
    if (temp_c >= FEVER_THRESHOLD_C) {
        TickType_t now = xTaskGetTickCount();
        if (s_fever_last_report == 0 ||
            (now - s_fever_last_report) >= pdMS_TO_TICKS(FEVER_COOLDOWN_MS)) {
            char json[64];
            snprintf(json, sizeof(json), "{\"body_temp\":%.1f}", temp_c);
            mqttClient_publish(TOPIC_FEVER, json);
            s_fever_last_report = now;
            ESP_LOGW(TAG, "发烧预警：体温 %.1f°C，已上报 %s", temp_c, TOPIC_FEVER);
        } else {
            ESP_LOGI(TAG, "发烧冷却中：体温 %.1f°C，距下次可报 %lus",
                     temp_c,
                     (unsigned long)(pdMS_TO_TICKS(FEVER_COOLDOWN_MS) - (now - s_fever_last_report))
                         / configTICK_RATE_HZ);
        }
    }
}

/*------------时间格式化------------*/
static void fmtTime(char *buf, const datetime_t *dt)
{
    snprintf(buf, 24, "%04u-%02u-%02u %02u:%02u", (unsigned)dt->y, (unsigned)dt->m,
             (unsigned)dt->d, (unsigned)dt->h, (unsigned)dt->min);
}

/*------------睡眠报告数据更新接口------------*/
void trans2cloud_updateSleepReport(const sleep_report_t *report)
{
    fmtTime(s_sr_bed,   &report->bed);
    fmtTime(s_sr_up,    &report->up);
    fmtTime(s_sr_sleep, &report->sleep);
    fmtTime(s_sr_wake,  &report->wake);
    s_sr_bed_mins   = report->bed_mins;
    s_sr_sleep_mins = report->sleep_mins;
    s_sr_awake_mins = report->awake_mins;
    s_sr_move_cnt   = report->move_cnt;
    s_sr_valid      = true;

    /* 独立报文立即发布 */
    char json[384];
    snprintf(json, sizeof(json),
        "{\"sr_valid\":true,"
        "\"sr_bed\":\"%s\",\"sr_up\":\"%s\","
        "\"sr_sleep\":\"%s\",\"sr_wake\":\"%s\","
        "\"sr_bed_mins\":%d,\"sr_sleep_mins\":%d,"
        "\"sr_awake_mins\":%d,\"sr_move_cnt\":%d}",
        s_sr_bed, s_sr_up, s_sr_sleep, s_sr_wake,
        s_sr_bed_mins, s_sr_sleep_mins,
        s_sr_awake_mins, s_sr_move_cnt);
    mqttClient_publish(TOPIC_SLEEP_REPORT, json);
}

/* 无效睡眠报告（如查询失败或无报告时调用） */
void trans2cloud_updateSleepReportEmpty(void)
{
    s_sr_valid = false;
}

/*------------连接的WiFi SSID 更新接口，上云显示在web------------*/
void trans2cloud_updateWifiSSID(const char *ssid)
{
    if (ssid) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';
    }
}

/* ─── 云端指令处理：命令表驱动 ────────────────────────────── */
/* 网页发来的指令格式: {"cmd":"led_onoff","value":1} 或 {"cmd":"led_mode","value":"rainbow"} */

typedef void (*cmd_handler_t)(int num_val, const char *str_val);

/* ── 场景指令（仅日志） ── */
static void h_scene_sleep(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到后台场景指令：%s (value=%d) <<<", v ? "睡着" : "醒来", v);
}
static void h_scene_cry(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到后台场景指令：哭闹 <<<");
}
static void h_scene_feed(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到后台场景指令：喂奶提醒 <<<");
}
static void h_scene_story(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到后台场景指令：播放故事 <<<");
}

/* ── LED 指令 ── */
static void h_led_onoff(int v, const char *s) {
    rgbLed_setOnOff(v != 0);
}
static void h_led_mode(int v, const char *s) {
    if (s) rgbLed_setMode(s);
}
static void h_led_brightness(int v, const char *s) {
    rgbLed_setBrightness((uint8_t)v);
}

/* ── 音量指令 ── */
static void h_volume(int v, const char *s) {
    wavPlayer_setVolume((uint8_t)v);
}

/* ── 毫米波睡眠指令（保留，当前未启用） ── */
static void h_mmwave_start(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到云端指令：开始睡眠记录 <<<");
#if 0
    mmWave_startSleep();
#endif
}
static void h_mmwave_end(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到云端指令：停止睡眠记录 <<<");
#if 0
    mmWave_endSleep();
#endif
}
static void h_mmwave_query(int v, const char *s) {
    ESP_LOGI(TAG, ">>> 收到云端指令：查询睡眠报告 <<<");
    s_sr_valid = false;
#if 0
    mmWave_querySleepReport();
#endif
}

/* ── 命令表 ── */
typedef struct {
    const char    *name;
    cmd_handler_t  handler;
} cmd_entry_t;

static const cmd_entry_t s_cmd_table[] = {
    /* 场景 */
    {"sleep",               h_scene_sleep},
    {"cry",                 h_scene_cry},
    {"feed",                h_scene_feed},
    {"story",               h_scene_story},
    /* LED */
    {"led_onoff",           h_led_onoff},
    {"led_mode",            h_led_mode},
    {"led_brightness",      h_led_brightness},
    /* 音频 */
    {"volume",              h_volume},
    /* 毫米波（保留，当前未启用） */
    {"mmwave_start_sleep",  h_mmwave_start},
    {"mmwave_end_sleep",    h_mmwave_end},
    {"mmwave_query_sleep",  h_mmwave_query},
};
#define CMD_COUNT (sizeof(s_cmd_table) / sizeof(s_cmd_table[0]))

/* ── 指令分发 ── */
static void onCloudCommand(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "收到云端指令: %s", payload);

    /* 提取 cmd */
    char cmd[32] = {0};
    const char *p = strstr(payload, "\"cmd\":\"");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != '"' && i < (int)sizeof(cmd) - 1) cmd[i++] = *p++;
    }
    if (strcmp(cmd, "classify_audio") == 0) {
        return;
    }

    /* 统一提取 value（数值 / 字符串） */
    int  num_val = 0;
    char str_val[16] = {0};
    p = strstr(payload, "\"value\":");
    if (p) {
        p += 8;
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(str_val) - 1) str_val[i++] = *p++;
        } else {
            num_val = atoi(p);
        }
    }

    /* 查表分发 */
    for (int i = 0; i < CMD_COUNT; i++) {
        if (strcmp(cmd, s_cmd_table[i].name) == 0) {
            s_cmd_table[i].handler(num_val, str_val[0] ? str_val : NULL);
            return;
        }
    }
    ESP_LOGW(TAG, "未知指令: %s", cmd);
}

/* ─── 音频数据接收与播放 ──────────────────────────────────── */
static uint8_t *s_audio_buf    = NULL;
static size_t   s_audio_size   = 0;
static size_t   s_audio_offset = 0;
static size_t   s_audio_chunk_cnt = 0;

static void onAudioStart(const char *name, size_t total_size)
{
    ESP_LOGI(TAG, "音频开始: %s (%u bytes)", name, (unsigned)total_size);

    if (s_audio_buf) {
        free(s_audio_buf);
        s_audio_buf = NULL;
    }
    s_audio_size   = 0;
    s_audio_offset = 0;

    if (total_size == 0 || total_size > 4 * 1024 * 1024) {
        ESP_LOGE(TAG, "音频大小无效: %u", (unsigned)total_size);
        return;
    }

    s_audio_buf = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_audio_buf) {
        s_audio_buf = malloc(total_size);
    }
    if (!s_audio_buf) {
        ESP_LOGE(TAG, "音频缓冲区分配失败 (%u bytes)", (unsigned)total_size);
        return;
    }
    s_audio_size      = total_size;
    s_audio_chunk_cnt = 0;
}

static void onAudioChunk(const uint8_t *data, size_t len)
{
    if (!s_audio_buf || s_audio_offset + len > s_audio_size) {
        ESP_LOGW(TAG, "忽略音频块 (buf=%p off=%u len=%u size=%u)",
                 (void *)s_audio_buf, (unsigned)s_audio_offset,
                 (unsigned)len, (unsigned)s_audio_size);
        return;
    }

    memcpy(s_audio_buf + s_audio_offset, data, len);
    s_audio_offset += len;
    s_audio_chunk_cnt++;

    unsigned pct = (unsigned)(s_audio_offset * 100 / s_audio_size);
    ESP_LOGI(TAG, "音频块 #%u: %u bytes, 进度 %u/%u (%u%%)",
             (unsigned)s_audio_chunk_cnt, (unsigned)len,
             (unsigned)s_audio_offset, (unsigned)s_audio_size, pct);

    if (s_audio_offset >= s_audio_size) {
        ESP_LOGI(TAG, "音频接收完成 (%u bytes)，开始播放", (unsigned)s_audio_size);
        wavPlayer_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        wavPlayer_play(s_audio_buf, s_audio_size);
    }
}

/* ─── 启动 ────────────────────────────────────────────────── */
esp_err_t trans2cloud_start(void)
{
    static bool started = false;
    if (started) {
        ESP_LOGI(TAG, "云端上报已在运行，跳过");
        return ESP_OK;
    }
    started = true;

    mqttClient_onCommand(onCloudCommand);
    mqttClient_onAudioStart(onAudioStart);
    mqttClient_onAudioChunk(onAudioChunk);


    xTaskCreate(reportTask, "rpt2cloud", 3072, NULL, 3, NULL);
    xTaskCreate(heartbeatTask, "hb2cloud", 2048, NULL, 2, NULL);

    
    ESP_LOGI(TAG, "Cloud data reporting started");
    return ESP_OK;
}
