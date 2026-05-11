#include "red_temp.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "red_temp";

static void redTempTask(void *arg)
{
    uint8_t buf[256];
    char hex[512];

    for (;;) {
        int len = uartDriver_read(RED_UART_NUM, buf, sizeof(buf) - 1, 200);

        if (len <= 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        int pos = 0;
        for (int i = 0; i < len && pos < (int)sizeof(hex) - 4; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);
        }

        ESP_LOGI(TAG, "%d 字节: %s", len, hex);
    }
}

esp_err_t redTemp_start(void)
{
    esp_err_t ret = uartDriver_init(RED_UART_NUM, RED_TX_PIN,
                                    RED_RX_PIN, RED_BAUD_RATE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "红外传感器 UART 初始化失败");
        return ret;
    }

    xTaskCreate(redTempTask, "red_temp", 2048, NULL, 2, NULL);
    ESP_LOGI(TAG, "轮询任务已启动");
    return ESP_OK;
}
