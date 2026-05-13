#include "red_temp.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

#define FRAME_LEN           10256
#define PIXEL_OFFSET        332
#define PIXEL_BYTES         (80 * 62 * 2)  // 9920
#define HEADER_LEN          12
#define TAIL_LEN            4

static const char *TAG = "red_temp";

static const uint8_t HEADER[HEADER_LEN] = {
    0x20, 0x20, 0x20, 0x23, 0x32, 0x38, 0x30, 0x38, 0x47, 0x46, 0x52, 0x41
};
static const uint8_t TAIL[TAIL_LEN] = { 0x58, 0x58, 0x58, 0x58 };

static uint16_t crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= (uint16_t)(data[i]) << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }
    return crc;
}

static int findHeader(const uint8_t *buf, int bufLen)
{
    for (int i = 0; i <= bufLen - HEADER_LEN; i++) {
        if (memcmp(buf + i, HEADER, HEADER_LEN) == 0)
            return i;
    }
    return -1;
}

static void parseFrame(const uint8_t *arr)
{
    int infoOff = 172;

    int frameNum  = arr[infoOff + 0]  | (arr[infoOff + 1]  << 8);
    double vdd    = (double)(arr[infoOff + 2]  | (arr[infoOff + 3]  << 8)) / 10000.0;
    double die    = (double)(arr[infoOff + 4]  | (arr[infoOff + 5]  << 8)) / 100.0 - 273.15;
    int timeStamp = arr[infoOff + 6]  | (arr[infoOff + 7]  << 8)
                  | (arr[infoOff + 8]  << 16) | (arr[infoOff + 9]  << 24);
    int pixMax    = (int16_t)(arr[infoOff + 10] | (arr[infoOff + 11] << 8));
    int pixMin    = (int16_t)(arr[infoOff + 12] | (arr[infoOff + 13] << 8));
    uint16_t crcExp = arr[infoOff + 14] | (arr[infoOff + 15] << 8);

    int bodyTemp  = arr[253] | (arr[254] << 8);
    int swVer     = arr[263] | (arr[264] << 8);
    int swDate    = arr[265] | (arr[266] << 8)
                  | (arr[267] << 16) | (arr[268] << 24);

    if (vdd < 3.0 || vdd > 3.6) {
        ESP_LOGW(TAG, "Vdd 异常: %.3fV", vdd);
        return;
    }
    if (die < -100.0 || die > 200.0) {
        ESP_LOGW(TAG, "Die temp 异常: %.1f°C", die);
        return;
    }
    if (pixMax < -3732 || pixMax > 12732) {
        ESP_LOGW(TAG, "PixMax 异常: %d", pixMax);
        return;
    }
    if (pixMin < -3732 || pixMin > 12732) {
        ESP_LOGW(TAG, "PixMin 异常: %d", pixMin);
        return;
    }

    uint16_t crcCalc = crc16(arr + PIXEL_OFFSET, PIXEL_BYTES);
    bool crcOk = (crcCalc == crcExp);

    float bodyTempC = (float)bodyTemp / 10.0f;
    int swMaj = swVer / 100;
    int swMin = (swVer / 10) % 10;
    int swPat = swVer % 10;

    ESP_LOGI(TAG, "#%d 体温:%.1f°C 传感器:%.1f°C Vdd:%.3fV Pix[%d,%d] CRC:%s SW:v%d.%d.%d",
             frameNum, bodyTempC, die, vdd, pixMin, pixMax,
             crcOk ? "OK" : "ERR", swMaj, swMin, swPat);
}

static void redTempTask(void *arg)
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

esp_err_t redTemp_start(void)
{
    esp_err_t ret = uartDriver_init(RED_UART_NUM, RED_TX_PIN,
                                    RED_RX_PIN, RED_BAUD_RATE,
                                    RED_RX_BUF_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "红外传感器 UART 初始化失败");
        return ret;
    }

    xTaskCreate(redTempTask, "red_temp", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "红外体温采集已启动");
    return ESP_OK;
}
