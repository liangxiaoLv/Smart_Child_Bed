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

#define PUBLISH_INTERVAL_MS  2000
#define HEARTBEAT_INTERVAL_MS 10000

static const char *TAG = "trans_cloud";

/* ─── 最新传感器数据（各传感器模块更新写入）───────────────── */
static float  s_env_temp  = 0;
static float  s_env_hum   = 0;
static uint8_t  s_aqi     = 0;
static uint16_t s_tvoc    = 0;
static uint16_t s_eco2    = 0;
static bool    s_presence = false;
static uint8_t s_breath   = 0;
static uint8_t s_heart    = 0;
static bool    s_move     = false;
static char    s_ssid[33] = "";

/* ─── 云端话题 ─────────────────────────────────────────────── */
#define TOPIC_STATUS    "bed/status"
#define TOPIC_HEARTBEAT "bed/heartbeat"

/* ─── 数据上报任务 ─────────────────────────────────────────── */
static void reportTask(void *arg)
{
    char json[512];
    for (;;) {
        snprintf(json, sizeof(json),
            "{\"ssid\":\"%s\","
            "\"env_temp\":%.1f,\"env_hum\":%.1f,"
            "\"aqi\":%d,\"tvoc\":%d,\"eco2\":%d,"
            "\"presence\":%d,\"breath\":%d,\"heart\":%d,\"move\":%d}",
            s_ssid,
            s_env_temp, s_env_hum,
            s_aqi, s_tvoc, s_eco2,
            s_presence, s_breath, s_heart, s_move);

        mqttClient_publish(TOPIC_STATUS, json);
        ESP_LOGI(TAG, "上报: %s", json);

        vTaskDelay(pdMS_TO_TICKS(PUBLISH_INTERVAL_MS));
    }
}

/* ─── 心跳任务 ────────────────────────────────────────────── */
static void heartbeatTask(void *arg)
{
    for (;;) {
        mqttClient_publish(TOPIC_HEARTBEAT, "\"online\"");
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}

/* ─── 数据更新接口（传感器模块调用）─────────────────────────── */
void trans2cloud_updateEnv(float temp_c, float hum_pct)
{
    s_env_temp = temp_c;
    s_env_hum  = hum_pct;
}

void trans2cloud_updateAir(uint8_t aqi, uint16_t tvoc_ppb, uint16_t eco2_ppm)
{
    s_aqi  = aqi;
    s_tvoc = tvoc_ppb;
    s_eco2 = eco2_ppm;
}

void trans2cloud_updateRadar(bool presence, uint8_t breath, uint8_t heart, bool move)
{
    s_presence = presence;
    s_breath   = breath;
    s_heart    = heart;
    s_move     = move;
}

void trans2cloud_updateWifiSSID(const char *ssid)
{
    if (ssid) {
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';
    }
}

/* ─── 云端指令处理 ────────────────────────────────────────── */
/* 网页发来的指令格式: {"cmd":"led_onoff","value":1} 等 */
static void onCloudCommand(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "收到云端指令: %s", payload);

    /* 提取 cmd 字段 */
    char cmd[32] = {0};
    const char *p = strstr(payload, "\"cmd\":\"");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != '"' && i < (int)sizeof(cmd) - 1) {
            cmd[i++] = *p++;
        }
    }

    /* 提取 value 数值 */
    int numVal = 0;
    p = strstr(payload, "\"value\":");
    if (p) {
        p += 8;
        numVal = atoi(p);
    }

    if (strcmp(cmd, "sleep") == 0) {
        ESP_LOGI(TAG, ">>> 收到后台场景指令：%s (value=%d) <<<",
                 numVal ? "睡着" : "醒来", numVal);
    } else if (strcmp(cmd, "cry") == 0) {
        ESP_LOGI(TAG, ">>> 收到后台场景指令：哭闹 <<<");
    } else if (strcmp(cmd, "feed") == 0) {
        ESP_LOGI(TAG, ">>> 收到后台场景指令：喂奶提醒 <<<");
    } else if (strcmp(cmd, "story") == 0) {
        ESP_LOGI(TAG, ">>> 收到后台场景指令：播放故事 <<<");
    } else if (strcmp(cmd, "led_onoff") == 0) {
        rgbLed_setOnOff(numVal != 0);
    } else if (strcmp(cmd, "led_mode") == 0) {
        /* value 是字符串，如 "rainbow" */
        p = strstr(payload, "\"value\":\"");
        if (p) {
            p += 9;
            char mode[16] = {0};
            int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(mode) - 1) {
                mode[i++] = *p++;
            }
            rgbLed_setMode(mode);
        }
    } else if (strcmp(cmd, "led_brightness") == 0) {
        rgbLed_setBrightness((uint8_t)numVal);
    } else if (strcmp(cmd, "volume") == 0) {
        wavPlayer_setVolume((uint8_t)numVal);
    } else {
        ESP_LOGW(TAG, "未知指令: %s", cmd);
    }
}

/* ─── 音频数据接收与播放 ──────────────────────────────────── */
static uint8_t *s_audio_buf    = NULL;
static size_t   s_audio_size   = 0;
static size_t   s_audio_offset = 0;

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
    s_audio_size = total_size;
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
    ESP_LOGI(TAG, "云端数据上报已启动");
    return ESP_OK;
}
