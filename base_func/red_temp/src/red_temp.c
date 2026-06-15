#include "red_temp.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "trans_2_cloud.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define FRAME_LEN           10256
#define HEADER_LEN          12
#define TAIL_LEN            4

static const char *TAG = "ir_temp";

static const uint8_t HEADER[HEADER_LEN] = {
    0x20, 0x20, 0x20, 0x23, 0x32, 0x38, 0x30, 0x38, 0x47, 0x46, 0x52, 0x41
};
static const uint8_t TAIL[TAIL_LEN] = { 0x58, 0x58, 0x58, 0x58 };

static int findHeader(const uint8_t *buf, int bufLen)
{
    for (int i = 0; i <= bufLen - HEADER_LEN; i++) {
        if (memcmp(buf + i, HEADER, HEADER_LEN) == 0)
            return i;
    }
    return -1;
}

static bool  IRTempIsValid(int bodyTemp)
{
    if (bodyTemp < 30){
        return false;
    }
    return true;
}

static void parseFrame(const uint8_t *arr)
{
    int infoOff = 172;

    int frameNum  = arr[infoOff + 0]  | (arr[infoOff + 1]  << 8);
    int bodyTemp  = arr[253] | (arr[254] << 8);
    float bodyTempC = (float)bodyTemp / 10.0f;
    ESP_LOGI(TAG, "#%d 体温:%.1f°C", frameNum, bodyTempC);

    if (IRTempIsValid(bodyTempC)) {
        trans2cloud_updateBodyTemp(bodyTempC);
    }
}

static void IRTempTask(void *arg)
{
    uint8_t *buf = malloc(FRAME_LEN * 2);
    if (!buf) {
        ESP_LOGE(TAG, "帧缓存分配失败");
        vTaskDelete(NULL);
        return;
    }
    int bufLen = 0;

    for (;;) {
        int n = uartDriver_read(RED_UART_NUM, buf + bufLen,
                                FRAME_LEN * 2 - bufLen, 500);
        if (n < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (n == 0)
            continue;

        bufLen += n;

        int hdrPos = findHeader(buf, bufLen);
        if (hdrPos < 0) {
            int keep = HEADER_LEN - 1;
            if (bufLen > keep) {
                memmove(buf, buf + bufLen - keep, keep);
                bufLen = keep;
            }
            continue;
        }

        if (bufLen - hdrPos < FRAME_LEN)
            continue;

        if (memcmp(buf + hdrPos + FRAME_LEN - TAIL_LEN, TAIL, TAIL_LEN) != 0) {
            int skip = hdrPos + 1;
            memmove(buf, buf + skip, bufLen - skip);
            bufLen -= skip;
            continue;
        }

        parseFrame(buf + hdrPos);

        int consumed = hdrPos + FRAME_LEN;
        memmove(buf, buf + consumed, bufLen - consumed);
        bufLen -= consumed;
    }

    free(buf);
    vTaskDelete(NULL);
}

esp_err_t IRTemp_start(void)
{
    esp_err_t ret = uartDriver_init(RED_UART_NUM, RED_TX_PIN,
                                    RED_RX_PIN, RED_BAUD_RATE,
                                    RED_RX_BUF_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Infrared temperature UART init failed! ret=%d", ret);
        return ret;
    }

    xTaskCreate(IRTempTask, "ir_temp", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "Infrared temperature task started");
    return ESP_OK;
}
