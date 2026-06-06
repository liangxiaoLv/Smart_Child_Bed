#include "mm_wave.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "trans_2_cloud.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <time.h>

static const char *TAG = "mm_wave";

/* ── 帧解析状态机 ─────────────────────────────────────────────── */
typedef enum {
    STATE_HEADER1,          /* 等待 0xAA */
    STATE_HEADER2,          /* 等待 0x55 */
    STATE_CMD,              /* 读取命令字 */
    STATE_LEN_H,            /* 读取长度高字节 */
    STATE_LEN_L,            /* 读取长度低字节 */
    STATE_DATA,             /* 读取数据载荷 */
    STATE_CHKSUM,           /* 校验 */
} parser_state_t;

static parser_state_t s_state = STATE_HEADER1;
static uint8_t s_cmd;
static uint16_t s_len;
static uint16_t s_data_pos;
static uint8_t s_data[512];

/* ── 校验和：Cmd + Len_H + Len_L + 数据段，取低8位 ──────────── */
static uint8_t calcChecksum(uint8_t cmd, uint16_t len, const uint8_t *data)
{
    uint32_t sum = cmd + ((len >> 8) & 0xFF) + (len & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/* ── 5秒发送间隔保护（模组每周期只响应第一条指令）───────────── */
static int64_t s_last_send_us = 0;
static int64_t s_last_auto_reply_us = 0;  /* 自动应答也限频，防止乒乓死锁 */

static bool cmdIntervalOk(void)
{
    int64_t now = esp_timer_get_time();
    if (s_last_send_us != 0 && (now - s_last_send_us) < 5000000) {
        int64_t remain = 5 - (now - s_last_send_us) / 1000000;
        ESP_LOGW(TAG, "指令过于频繁，请%lld秒后再试", remain);
        return false;
    }
    s_last_send_us = now;
    return true;
}

/* ── 帧处理 ──────────────────────────────────────────────────── */
static void handleFrame(uint8_t cmd, uint16_t len, const uint8_t *data)
{
    switch (cmd) {
    case 0x01:  /* 软硬件版本号 */
        if (len >= 6) {
            ESP_LOGI(TAG, "版本: SW v%d.%d.%d  HW v%d.%d.%d",
                     data[0], data[1], data[2],
                     data[3], data[4], data[5]);
        }
        break;

    case 0x02:  /* 心跳包 / 实时数据包 (5字节, 5秒周期) */
        if (len >= 5) {
            const char *person;
            switch (data[0]) {
            case 0: person = "无人";   break;
            case 1: person = "有人";   break;
            case 2: person = "干扰";   break;
            default: person = "未知";  break;
            }

            const char *motion;
            switch (data[3]) {
            case 0: motion = "无体动"; break;
            case 1: motion = "小体动"; break;
            case 2: motion = "大体动"; break;
            default: motion = "未知";  break;
            }

            const char *modStatus;
            switch (data[4]) {
            case 0: modStatus = "未监测(无报告)"; break;
            case 1: modStatus = "监测中";         break;
            case 2: modStatus = "未监测(有报告)"; break;
            case 3: modStatus = "等待时间输入";   break;
            default: modStatus = "未知";          break;
            }

            ESP_LOGI(TAG, "体征: %s | 呼吸 %d | 心率 %d | %s | 状态:%s",
                     person, data[1], data[2], motion, modStatus);

            trans2cloud_updateRadarFull(data[0], data[1], data[2],
                                        data[3], data[4]);
        }
        break;

    case 0x03:
        if (len == 0) {
            /* 自动应答也限频：最少间隔 5 秒，防止乒乓死锁 */
            int64_t now_us = esp_timer_get_time();
            if (s_last_auto_reply_us != 0 && (now_us - s_last_auto_reply_us) < 5000000) {
                ESP_LOGW(TAG, "自动应答过于频繁，跳过（距上次仅 %lld 秒）",
                         (now_us - s_last_auto_reply_us) / 1000000);
                break;
            }
            s_last_auto_reply_us = now_us;

            ESP_LOGI(TAG, "模组请求绝对时间，自动应答");
            time_t now;
            time(&now);
            struct tm ti;
            localtime_r(&now, &ti);
            uint16_t y = ti.tm_year + 1900;
            uint8_t time_cmd[] = {
                0xAA, 0x55, 0x03,
                0x00, 0x06,
                (y >> 8) & 0xFF, y & 0xFF,
                ti.tm_mon + 1, ti.tm_mday,
                ti.tm_hour, ti.tm_min,
                0x00  /* CRC placeholder */
            };
            time_cmd[sizeof(time_cmd) - 1] = calcChecksum(0x03, 6, time_cmd + 5);
            mmWave_send(time_cmd, sizeof(time_cmd));
        } else if (len >= 1) {
            ESP_LOGI(TAG, "时间设置应答: %s", data[0] ? "成功" : "失败(格式错误或未开始记录?)");
        }
        break;

    case 0x04:  /* 设备ID */
        if (len >= 4) {
            uint32_t dev_id = ((uint32_t)data[0] << 24) |
                              ((uint32_t)data[1] << 16) |
                              ((uint32_t)data[2] << 8) |
                              data[3];
            ESP_LOGI(TAG, "设备ID: %lu (0x%08lX)", dev_id, dev_id);
        }
        break;

    case 0x05:  /* 开始记录睡眠数据应答 */
        if (len >= 1) {
            ESP_LOGI(TAG, "睡眠监测: %s",
                     data[0] ? "开始记录" : "失败(已在记录中?)");
            trans2cloud_updateSleepRecord(data[0] != 0);

            /* 开始成功后主动发送当前时间 */
            if (data[0]) {
                time_t now;
                time(&now);
                struct tm ti;
                localtime_r(&now, &ti);
                ESP_LOGI(TAG, "自动发送时间: %04d-%02d-%02d %02d:%02d",
                         ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                         ti.tm_hour, ti.tm_min);
                uint8_t time_cmd[] = {
                    0xAA, 0x55, 0x03,
                    0x00, 0x06,
                    (uint16_t)(ti.tm_year + 1900) >> 8,
                    (uint16_t)(ti.tm_year + 1900) & 0xFF,
                    ti.tm_mon + 1, ti.tm_mday,
                    ti.tm_hour, ti.tm_min,
                    0x00
                };
                time_cmd[sizeof(time_cmd) - 1] = calcChecksum(0x03, 6, time_cmd + 5);
                mmWave_send(time_cmd, sizeof(time_cmd));
            }
        }
        break;

    case 0x06:  /* 结束记录睡眠数据应答 */
        if (len >= 1) {
            ESP_LOGI(TAG, "睡眠记录: %s",
                     data[0] ? "已结束" : "失败(未开启或无有效时间?)");
            if (data[0]) {
                trans2cloud_updateSleepRecord(false);
            }
        }
        break;

    case 0x07:  /* 睡眠报告 (32字节) */
        if (len == 32) {
            ESP_LOGI(TAG, "══════ 睡眠报告 原始数据 ══════");
            ESP_LOG_BUFFER_HEX(TAG, data, len);

            uint16_t bed_y   = (data[0] << 8) | data[1];
            uint16_t up_y    = (data[6] << 8) | data[7];
            uint16_t sleep_y = (data[12] << 8) | data[13];
            uint16_t wake_y  = (data[18] << 8) | data[19];

            ESP_LOGI(TAG, "═══ 睡眠报告 ═══");
            ESP_LOGI(TAG, "上床: %04d/%02d/%02d %02d:%02d  下床: %04d/%02d/%02d %02d:%02d",
                     bed_y, data[2], data[3], data[4], data[5],
                     up_y, data[8], data[9], data[10], data[11]);
            ESP_LOGI(TAG, "入睡: %04d/%02d/%02d %02d:%02d  醒来: %04d/%02d/%02d %02d:%02d",
                     sleep_y, data[14], data[15], data[16], data[17],
                     wake_y, data[20], data[21], data[22], data[23]);

            uint16_t bed_mins   = (data[24] << 8) | data[25];
            uint16_t sleep_mins = (data[26] << 8) | data[27];
            uint16_t awake_mins = (data[28] << 8) | data[29];
            uint16_t move_cnt   = (data[30] << 8) | data[31];

            ESP_LOGI(TAG, "卧床 %d分 | 睡眠 %d分 | 清醒 %d分 | 体动 %d次",
                     bed_mins, sleep_mins, awake_mins, move_cnt);

            sleep_report_t report = {
                .bed   = { .y = bed_y,   .m = data[2],  .d = data[3],  .h = data[4],  .min = data[5] },
                .up    = { .y = up_y,    .m = data[8],  .d = data[9],  .h = data[10], .min = data[11] },
                .sleep = { .y = sleep_y, .m = data[14], .d = data[15], .h = data[16], .min = data[17] },
                .wake  = { .y = wake_y,  .m = data[20], .d = data[21], .h = data[22], .min = data[23] },
                .bed_mins = bed_mins, .sleep_mins = sleep_mins,
                .awake_mins = awake_mins, .move_cnt = move_cnt,
            };
            trans2cloud_updateSleepReport(&report);
        } else if (len == 1) {
            ESP_LOGI(TAG, "睡眠报告: %s",
                     data[0] ? "有数据" : "无有效睡眠数据(未结束或未监测到有效睡眠)");
            trans2cloud_updateSleepReportEmpty();
        }
        break;

    default: {
        ESP_LOGI(TAG, "未知帧 CMD=0x%02X len=%d 数据:", cmd, len);
        ESP_LOG_BUFFER_HEX(TAG, data, len);
        break;
    }
    }
}

/* ── 解析器：逐字节喂入 ──────────────────────────────────────── */
static void parserFeed(uint8_t byte)
{
    switch (s_state) {
    case STATE_HEADER1:
        if (byte == 0xAA) {
            s_state = STATE_HEADER2;
        }
        break;

    case STATE_HEADER2:
        s_state = (byte == 0x55) ? STATE_CMD : STATE_HEADER1;
        break;

    case STATE_CMD:
        s_cmd = byte;
        s_state = STATE_LEN_H;
        break;

    case STATE_LEN_H:
        s_len = (uint16_t)byte << 8;
        s_state = STATE_LEN_L;
        break;

    case STATE_LEN_L:
        s_len |= byte;
        s_data_pos = 0;
        s_state = (s_len > 0 && s_len <= sizeof(s_data)) ? STATE_DATA : STATE_HEADER1;
        break;

    case STATE_DATA:
        s_data[s_data_pos++] = byte;
        if (s_data_pos >= s_len) {
            s_state = STATE_CHKSUM;
        }
        break;

    case STATE_CHKSUM: {
        uint8_t expected = calcChecksum(s_cmd, s_len, s_data);
        if (byte == expected) {
            handleFrame(s_cmd, s_len, s_data);
        } else {
            ESP_LOGW(TAG, "校验失败 cmd=0x%02X 期望 0x%02X 收到 0x%02X",
                     s_cmd, expected, byte);
        }
        s_state = STATE_HEADER1;
        break;
    }
    }
}

/* ── 接收任务 ────────────────────────────────────────────────── */
static void mmWaveTask(void *arg)
{
    uint8_t buf[128];

    for (;;) {
        int len = uartDriver_read(RADAR_UART_NUM, buf, sizeof(buf), 100);

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                parserFeed(buf[i]);
            }
            vTaskDelay(1);   /* 让出 CPU 给 console task */
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ── 公共接口 ────────────────────────────────────────────────── */

esp_err_t mm_wave_radar_info(void)
{
    esp_err_t ret = uartDriver_init(RADAR_UART_NUM, RADAR_TX_PIN,
                                    RADAR_RX_PIN, RADAR_BAUD_RATE,
                                    RADAR_RX_BUF_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "毫米波雷达 UART 初始化失败");
        return ret;
    }

    xTaskCreate(mmWaveTask, "mm_wave", 3072, NULL, 1, NULL);
    ESP_LOGI(TAG, "雷达接收任务已启动");
    return ESP_OK;
}

esp_err_t mmWave_send(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "发送 %d 字节", (int)len);
    return uartDriver_write(RADAR_UART_NUM, data, len);
}

esp_err_t mmWave_queryVersion(void)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = { 0xAA, 0x55, 0x01, 0x00, 0x00, 0x00 };
    cmd[5] = calcChecksum(0x01, 0, NULL);
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_queryDeviceId(void)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = { 0xAA, 0x55, 0x04, 0x00, 0x00, 0x00 };
    cmd[5] = calcChecksum(0x04, 0, NULL);
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_startSleep(void)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = { 0xAA, 0x55, 0x05, 0x00, 0x00, 0x00 };
    cmd[5] = calcChecksum(0x05, 0, NULL);
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_endSleep(void)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = { 0xAA, 0x55, 0x06, 0x00, 0x00, 0x00 };
    cmd[5] = calcChecksum(0x06, 0, NULL);
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_querySleepReport(void)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = { 0xAA, 0x55, 0x07, 0x00, 0x00, 0x00 };
    cmd[5] = calcChecksum(0x07, 0, NULL);
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_setTime(uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute)
{
    if (!cmdIntervalOk()) return ESP_ERR_INVALID_STATE;
    uint8_t cmd[] = {
        0xAA, 0x55, 0x03,
        0x00, 0x06,
        (year >> 8) & 0xFF, year & 0xFF,
        month, day, hour, minute,
        0x00  /* CRC placeholder */
    };
    cmd[sizeof(cmd) - 1] = calcChecksum(0x03, 6, cmd + 5);
    return mmWave_send(cmd, sizeof(cmd));
}
