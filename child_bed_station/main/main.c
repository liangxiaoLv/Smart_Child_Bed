#include "wifi_connect.h"
#include "cloud_mqtt.h"
#include "buzzer.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "main";

static void onCloudCommand(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "收到云端指令: %s", payload);

    char cmd[32] = {0};
    const char *p = strstr(payload, "\"cmd\":\"");
    if (p) {
        p += 7;
        int i = 0;
        while (*p && *p != '"' && i < (int)sizeof(cmd) - 1) {
            cmd[i++] = *p++;
        }
    }

    int numVal = 0;
    p = strstr(payload, "\"value\":");
    if (p) {
        p += 8;
        numVal = atoi(p);
    }

    if (strcmp(cmd, "warning") == 0) {
        buzzer_setWarning(numVal != 0);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "child_bed_station starting...");

    buzzer_init();

    wifiConnect_init();

    mqttClient_onCommand(onCloudCommand);
    mqttClient_start();

    ESP_LOGI(TAG, "WiFi init done");
}
