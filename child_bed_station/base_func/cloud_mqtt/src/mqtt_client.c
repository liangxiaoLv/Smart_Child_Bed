#include "cloud_mqtt.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <string.h>
#include <stdlib.h>

/* ─── 服务器配置 ────────────────────────────────────────── */
#define MQTT_BROKER_URI  "mqtt://60.205.235.150:1883"
#define MQTT_USERNAME    "childbe"
#define MQTT_PASSWORD    "Yinta"

#define TOPIC_CONTROL    "station/control"
#define MQTT_QOS         1

static const char *TAG = "mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static void (*s_cmd_cb)(const char *topic, const char *payload);

static void mqttEventHandler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT 已连接 Broker");
        esp_mqtt_client_subscribe(s_client, TOPIC_CONTROL, MQTT_QOS);
        ESP_LOGI(TAG, "已订阅: %s", TOPIC_CONTROL);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT 断开，自动重连中...");
        break;

    case MQTT_EVENT_DATA: {
        char topic[ev->topic_len + 1];
        char *payload = malloc(ev->data_len + 1);
        if (!payload) break;
        memcpy(topic, ev->topic, ev->topic_len);
        topic[ev->topic_len] = '\0';
        memcpy(payload, ev->data, ev->data_len);
        payload[ev->data_len] = '\0';

        ESP_LOGI(TAG, "收到 [%s]: %s", topic, payload);

        if (strcmp(topic, TOPIC_CONTROL) == 0 && s_cmd_cb) {
            s_cmd_cb(topic, payload);
        }
        free(payload);
        break;
    }

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        break;

    default:
        break;
    }
}

static void getClientId(char *buf, size_t max)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(buf, max, "station_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

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
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, MQTT_QOS, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "发布失败, topic=%s", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void mqttClient_onCommand(void (*cb)(const char *topic, const char *payload))
{
    s_cmd_cb = cb;
}
