#include "cloud_mqtt.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdlib.h>

/* ─── 服务器配置（根据实际情况修改）────────────────────────── */
#define MQTT_BROKER_URI  "mqtt://60.205.235.150:1883"
#define MQTT_USERNAME    "childbe"
#define MQTT_PASSWORD    "Yinta"

/* ─── 本地话题 ────────────────────────────────────────────── */
#define TOPIC_CONTROL      "bed/control"
#define TOPIC_STATUS       "bed/status"
#define TOPIC_HEARTBEAT    "bed/heartbeat"
#define TOPIC_AUDIO_START  "bed/audio_start"
#define TOPIC_AUDIO        "bed/audio"

static const char *TAG = "mqtt";

/* ─── 模块状态 ────────────────────────────────────────────── */
static esp_mqtt_client_handle_t s_client = NULL;
#define CMD_CB_MAX 4
static void (*s_cmd_cbs[CMD_CB_MAX])(const char *topic, const char *payload);
static void (*s_audio_start_cb)(const char *name, size_t total_size);
static void (*s_audio_chunk_cb)(const uint8_t *data, size_t len);

/* ─── 话题匹配 ────────────────────────────────────────────── */
static bool topicMatch(const char *topic, int topic_len, const char *expected)
{
    size_t elen = strlen(expected);
    if ((size_t)topic_len != elen) return false;
    return memcmp(topic, expected, elen) == 0;
}

/* ─── MQTT 事件处理 ───────────────────────────────────────── */
static void mqttEventHandler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT 已连接 Broker");
        esp_mqtt_client_subscribe(s_client, TOPIC_CONTROL, MQTT_QOS_AT_LEAST_ONCE);
        esp_mqtt_client_subscribe(s_client, TOPIC_AUDIO_START, MQTT_QOS_AT_LEAST_ONCE);
        esp_mqtt_client_subscribe(s_client, TOPIC_AUDIO, MQTT_QOS_AT_LEAST_ONCE);
        ESP_LOGI(TAG, "已订阅: %s, %s, %s", TOPIC_CONTROL, TOPIC_AUDIO_START, TOPIC_AUDIO);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 断开，自动重连中...");
        break;

    case MQTT_EVENT_DATA:
        /* bed/audio — 原始二进制，直接传指针不复制 */
        if (topicMatch(ev->topic, ev->topic_len, TOPIC_AUDIO)) {
            if (s_audio_chunk_cb) {
                s_audio_chunk_cb((const uint8_t *)ev->data, ev->data_len);
            }
            break;
        }

        /* 其余话题 — 文本，复制为 C 字符串 */
        {
            char topic[ev->topic_len + 1];
            char *payload = malloc(ev->data_len + 1);
            if (!payload) break;
            memcpy(topic, ev->topic, ev->topic_len);
            topic[ev->topic_len] = '\0';
            memcpy(payload, ev->data, ev->data_len);
            payload[ev->data_len] = '\0';

            ESP_LOGI(TAG, "收到 [%s]: %s", topic, payload);

            if (strcmp(topic, TOPIC_AUDIO_START) == 0 && s_audio_start_cb) {
                /* 解析 {"name":"...", "size":N} — 兼容带空格 */
                char name[128] = {0};
                size_t fsize = 0;
                const char *p = strstr(payload, "\"name\"");
                if (p) {
                    p = strchr(p, ':');
                    if (p) {
                        p = strchr(p, '"');
                        if (p) {
                            p++;
                            int i = 0;
                            while (*p && *p != '"' && i < (int)sizeof(name) - 1)
                                name[i++] = *p++;
                        }
                    }
                }
                p = strstr(payload, "\"size\"");
                if (p) {
                    p = strchr(p, ':');
                    if (p) fsize = (size_t)atol(p + 1);
                }
                s_audio_start_cb(name, fsize);
            } else if (strcmp(topic, TOPIC_CONTROL) == 0) {
                for (int i = 0; i < CMD_CB_MAX; i++) {
                    if (s_cmd_cbs[i]) {
                        s_cmd_cbs[i](topic, payload);
                    }
                }
            }

            free(payload);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;

    default:
        break;
    }
}

/* ─── 生成客户端 ID ───────────────────────────────────────── */
static void getClientId(char *buf, size_t max)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, max, "esp32_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

/* ─── 公共 API ────────────────────────────────────────────── */

esp_err_t mqttClient_start(void)
{
    if (s_client) {
        ESP_LOGI(TAG, "MQTT 客户端已在运行，跳过");
        return ESP_OK;
    }

    char client_id[32];
    getClientId(client_id, sizeof(client_id));
    ESP_LOGI(TAG, "启动 MQTT 客户端, ID: %s", client_id);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
        .session.keepalive = 30,
        .buffer.size = 16384,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqttEventHandler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t mqttClient_publish(const char *topic, const char *payload)
{
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT 未初始化，无法发布");
        return ESP_FAIL;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, MQTT_QOS_AT_LEAST_ONCE, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布失败, topic=%s", topic);
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "已发布 [%s] (msg_id=%d)", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqttClient_publishBinary(const char *topic, const uint8_t *data, size_t len)
{
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT 未初始化，无法发布");
        return ESP_FAIL;
    }
    int msg_id = esp_mqtt_client_publish(s_client, topic, (const char *)data, (int)len, MQTT_QOS_AT_LEAST_ONCE, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布二进制失败, topic=%s, len=%d", topic, (int)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void mqttClient_onCommand(void (*cb)(const char *topic, const char *payload))
{
    if (!cb) {
        return;
    }

    for (int i = 0; i < CMD_CB_MAX; i++) {
        if (s_cmd_cbs[i] == cb) {
            return;
        }
    }
    for (int i = 0; i < CMD_CB_MAX; i++) {
        if (!s_cmd_cbs[i]) {
            s_cmd_cbs[i] = cb;
            return;
        }
    }
    ESP_LOGW(TAG, "command callback slots full");
}

void mqttClient_onAudioStart(void (*cb)(const char *name, size_t total_size))
{
    s_audio_start_cb = cb;
}

void mqttClient_onAudioChunk(void (*cb)(const uint8_t *data, size_t len))
{
    s_audio_chunk_cb = cb;
}
