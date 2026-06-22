#include "wifi_connect.h"
#include "cloud_mqtt.h"
#include "buzzer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#define TEST_LED_PIN    GPIO_NUM_2      /* IO2 — LED 正极，负极接地，高电平点亮 */
#define BTN_PIN         GPIO_NUM_3      /* IO3 — 按钮，按下接地（低电平有效），内部上拉 */

static const char *TAG = "main";

static volatile bool s_alerting = false;

/* 统一控制告警状态：蜂鸣器 + LED 同步 */
static void setAlert(bool on)
{
    s_alerting = on;
    buzzer_setWarning(on);
    gpio_set_level(TEST_LED_PIN, on ? 1 : 0);
    ESP_LOGI(TAG, "Alert %s", on ? "ON" : "OFF");
}

/* 按钮轮询任务：检测下降沿，消抖后取消告警 */
static void btnTask(void *arg)
{
    bool last = true;
    for (;;) {
        bool cur = (bool)gpio_get_level(BTN_PIN);
        if (last && !cur) {             /* 下降沿 */
            vTaskDelay(pdMS_TO_TICKS(20)); /* 消抖 20ms */
            if (!gpio_get_level(BTN_PIN) && s_alerting) {
                setAlert(false);
            }
        }
        last = cur;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void onCloudCommand(const char *topic, const char *payload)
{
    ESP_LOGI(TAG, "收到云端指令: %s", payload);

    char cmd[32] = {0};
    const char *p = strstr(payload, "\"cmd\":");
    if (p) {
        p += 6;
        while (*p == ' ') p++;   /* 跳过冒号后空格 */
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < (int)sizeof(cmd) - 1) {
                cmd[i++] = *p++;
            }
        }
    }

    int numVal = 0;
    p = strstr(payload, "\"value\":");
    if (p) {
        p += 8;
        while (*p == ' ') p++;   /* 跳过冒号后空格 */
        numVal = atoi(p);
    }

    if (strcmp(cmd, "warning") == 0) {
        setAlert(numVal != 0);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "child_bed_station starting...");

    /* LED 初始化，默认熄灭 */
    gpio_config_t led_cfg = {
        .pin_bit_mask = BIT64(TEST_LED_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_cfg);
    gpio_set_level(TEST_LED_PIN, 0);

    /* 按钮初始化，内部上拉，按下低电平 */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = BIT64(BTN_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    buzzer_init();
    xTaskCreate(btnTask, "btn", 1536, NULL, 2, NULL);

    wifiConnect_init();

    mqttClient_onCommand(onCloudCommand);
    mqttClient_start();

    ESP_LOGI(TAG, "WiFi init done");
}
