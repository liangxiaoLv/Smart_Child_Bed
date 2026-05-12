#include "mm_wave.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

/* ── 校验和 ──────────────────────────────────────────────────── */
static uint8_t calcChecksum(uint8_t cmd, uint16_t len, const uint8_t *data)
{
    uint32_t sum = cmd + ((len >> 8) & 0xFF) + (len & 0xFF);
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/* ── 帧处理 ──────────────────────────────────────────────────── */
static void handleFrame(uint8_t cmd, uint16_t len, const uint8_t *data)
{
    switch (cmd) {
    case 0x01:  /* 版本信息 */
        if (len >= 6) {
            ESP_LOGI(TAG, "版本信息: 软件 v%d.%d.%d  硬件 v%d.%d.%d",
                     data[0], data[1], data[2],
                     data[3], data[4], data[5]);
        }
        break;

    case 0x02:  /* 实时生命体征 (4 字节) */
        if (len >= 4) {
            const char *person = data[0] ? "有人" : "无人";
            const char *move  = data[3] ? "发生体动" : "未发生体动";
            ESP_LOGI(TAG, "生命体征: %s | 呼吸率 %d | 心率 %d | %s",
                     person, data[1], data[2], move);
        }
        break;

    case 0x03:  /* 时间设置响应 */
        if (len >= 1) {
            ESP_LOGI(TAG, "时间设置: %s", data[0] ? "成功" : "失败");
        }
        break;

    case 0x05:  /* 睡眠监测开始响应 */
        if (len >= 1) {
            ESP_LOGI(TAG, "睡眠监测: %s", data[0] ? "开始记录" : "失败");
        }
        break;

    case 0x06:  /* 睡眠监测结束响应 */
        if (len >= 1) {
            ESP_LOGI(TAG, "睡眠记录: %s", data[0] ? "结束" : "失败");
        }
        break;

    case 0x07:  /* 睡眠报告数值 (42 字节) */
        if (len >= 42) {
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
            ESP_LOGI(TAG, "卧床 %d分 | 离床 %d次 | 睡眠 %d分 | 清醒 %d分 | 浅睡 %d分 | 深睡 %d分",
                     data[25], data[27], data[28], data[30], data[32], data[34]);
            ESP_LOGI(TAG, "呼吸暂停 %d次 | 最长 %d秒 | 平均 %d秒",
                     data[36], data[37], data[38]);
            ESP_LOGI(TAG, "睡眠评分 %d | 呼吸评分 %d | HRV %dms",
                     data[39], data[40], data[41]);
        }
        break;

    case 0x08:  /* 睡眠分期数据 */
        if (len >= 1) {
            char stages[256] = {0};
            int pos = 0;
            for (uint16_t i = 0; i < len && pos < (int)sizeof(stages) - 12; i++) {
                const char *s;
                switch (data[i]) {
                case 0: s = "醒"; break;
                case 1: s = "浅睡"; break;
                case 2: s = "深睡"; break;
                case 3: s = "REM"; break;
                default: s = "?"; break;
                }
                pos += snprintf(stages + pos, sizeof(stages) - pos, "%s ", s);
            }
            ESP_LOGI(TAG, "睡眠分期(%d分钟): %s", len, stages);
        }
        break;

    default: {
        char hex[256];
        int pos = 0;
        for (uint16_t i = 0; i < len && pos < (int)sizeof(hex) - 4; i++) {
            pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[i]);
        }
        ESP_LOGI(TAG, "未知帧 CMD=0x%02X 数据: %s", cmd, hex);
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
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

/* ── 控制台交互任务 ──────────────────────────────────────────── */
static void mmWaveConsoleTask(void *arg)
{
    printf("\n========== 毫米波雷达交互菜单 ==========\n");
    printf("1 - 查询版本\n");
    printf("2 - 开始睡眠监测\n");
    printf("3 - 结束睡眠记录\n");
    printf("4 - 查询睡眠报告\n");
    printf("5 - 查询睡眠分期\n");
    printf("6 - 设置时间\n");
    printf("=========================================\n");
    printf("请按键选择: ");
    fflush(stdout);

    char buf[16];
    for (;;) {
        if (fgets(buf, sizeof(buf), stdin) != NULL) {
            int key = atoi(buf);
            switch (key) {
            case 1: mmWave_queryVersion();      break;
            case 2: mmWave_startSleep();        break;
            case 3: mmWave_endSleep();          break;
            case 4: mmWave_querySleepReport();  break;
            case 5: mmWave_querySleepStage();   break;
            case 6: mmWave_setTime(2026, 5, 11, 12, 0); break;
            default:
                printf("无效按键，请重试: ");
                fflush(stdout);
                continue;
            }
            printf("命令已发送\n请按键选择: ");
            fflush(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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

    xTaskCreate(mmWaveTask, "mm_wave", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "雷达接收任务已启动");

    xTaskCreate(mmWaveConsoleTask, "mm_console", 3072, NULL, 2, NULL);
    ESP_LOGI(TAG, "雷达控制台已启动");
    return ESP_OK;
}

esp_err_t mmWave_send(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "发送 %d 字节", (int)len);
    return uartDriver_write(RADAR_UART_NUM, data, len);
}

esp_err_t mmWave_queryVersion(void)
{
    uint8_t cmd[] = { 0xAA, 0x55, 0x01 };
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_startSleep(void)
{
    uint8_t cmd[] = { 0xAA, 0x55, 0x05 };
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_endSleep(void)
{
    uint8_t cmd[] = { 0xAA, 0x55, 0x06 };
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_querySleepReport(void)
{
    uint8_t cmd[] = { 0xAA, 0x55, 0x07 };
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_querySleepStage(void)
{
    uint8_t cmd[] = { 0xAA, 0x55, 0x08 };
    return mmWave_send(cmd, sizeof(cmd));
}

esp_err_t mmWave_setTime(uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute)
{
    uint8_t cmd[] = {
        0xAA, 0x55, 0x03,
        0x00, 0x06,
        (year >> 8) & 0xFF, year & 0xFF,
        month, day, hour, minute
    };
    return mmWave_send(cmd, sizeof(cmd));
}
