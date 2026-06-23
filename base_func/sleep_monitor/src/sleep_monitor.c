#include "sleep_monitor.h"
#include "pin_map.h"
#include "uart_driver.h"
#include "trans_2_cloud.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "sleep_monitor";

/* ── CRC32 查表（Poly=0x04C11DB7, RefIn=true, RefOut=true, Init=0xFFFFFFFF, XorOut=0xFFFFFFFF）── */
#define CRC32_POLY  0x04C11DB7
#define CRC32_INIT  0xFFFFFFFF
#define CRC32_XOR   0xFFFFFFFF

static uint32_t s_crc32Table[256];

static uint8_t reflect8(uint8_t v)
{
    v = ((v & 0x55) << 1) | ((v & 0xAA) >> 1);
    v = ((v & 0x33) << 2) | ((v & 0xCC) >> 2);
    return ((v & 0x0F) << 4) | ((v & 0xF0) >> 4);
}

static uint32_t reflect32(uint32_t v)
{
    v = ((v & 0x55555555) << 1) | ((v & 0xAAAAAAAA) >> 1);
    v = ((v & 0x33333333) << 2) | ((v & 0xCCCCCCCC) >> 2);
    v = ((v & 0x0F0F0F0F) << 4) | ((v & 0xF0F0F0F0) >> 4);
    v = ((v & 0x00FF00FF) << 8) | ((v & 0xFF00FF00) >> 8);
    return ((v & 0x0000FFFF) << 16) | ((v & 0xFFFF0000) >> 16);
}

static void crc32Init(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t crc = (uint32_t)i << 24;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x80000000) {
                crc = (crc << 1) ^ CRC32_POLY;
            } else {
                crc <<= 1;
            }
        }
        s_crc32Table[i] = crc;
    }
}

static uint32_t crc32Calc(const uint8_t *data, uint16_t len)
{
    uint32_t crc = CRC32_INIT;
    for (uint16_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ s_crc32Table[((crc >> 24) ^ reflect8(data[i])) & 0xFF];
    }
    return reflect32(crc) ^ CRC32_XOR;
}

/* ── 协议常量 ──────────────────────────────────────────────────── */
#define FRAME_HEAD0  0xAA
#define FRAME_HEAD1  0x55
#define FRAME_TAIL0  0xFE
#define FRAME_TAIL1  0xEF
#define FRAME_VER    0x01
#define FRAME_MIN    18
#define FRAME_MAX    2048

/* ── 解析后的帧结构 ────────────────────────────────────────────── */
typedef struct {
    uint8_t  ver;
    uint16_t type;
    uint16_t seq;
    uint8_t  channel;
    uint16_t command;
    uint8_t *arg;
    uint16_t arg_len;
} bcgFrame_t;

static void frameFree(bcgFrame_t *f)
{
    if (f) {
        if (f->arg) free(f->arg);
        free(f);
    }
}

/* ── 帧解析状态机（参照 mm_wave parserFeed 模式）──────────────── */
typedef enum {
    SYNC_HEAD0,
    SYNC_HEAD1,
    SYNC_LEN_H,
    SYNC_LEN_L,
    SYNC_DATA,
} syncState_t;

static syncState_t s_syncState = SYNC_HEAD0;
static uint16_t    s_frameLen;
static uint16_t    s_dataPos;
static uint8_t     s_frameBuf[FRAME_MAX];

static uint16_t s_seq = 0;

/* ── 帧队列（RX Task → 查询 / pollRx）────────────────────────── */
static QueueHandle_t s_frameQueue;
static TaskHandle_t s_pollTask = NULL;

/* ── 帧解析：原始字节 → bcgFrame_t ────────────────────────────── */
static bcgFrame_t *frameParse(const uint8_t *raw, uint16_t len)
{
    /* Tail 校验 */
    if (raw[len - 2] != FRAME_TAIL0 || raw[len - 1] != FRAME_TAIL1) {
        ESP_LOGW(TAG, "帧尾不匹配: %02X%02X", raw[len - 2], raw[len - 1]);
        return NULL;
    }

    /* CRC32 校验（覆盖 [0, len-7] 共 len-6 字节） */
    uint32_t crcCalc = crc32Calc(raw, len - 6);
    uint32_t crcFrame = ((uint32_t)raw[len - 6] << 24) |
                        ((uint32_t)raw[len - 5] << 16) |
                        ((uint32_t)raw[len - 4] << 8) |
                        raw[len - 3];
    if (crcCalc != crcFrame) {
        ESP_LOGW(TAG, "CRC 校验失败 calc=0x%08lX frame=0x%08lX", crcCalc, crcFrame);
        return NULL;
    }

    bcgFrame_t *f = calloc(1, sizeof(bcgFrame_t));
    if (!f) return NULL;

    f->ver     = raw[4];
    f->type    = ((uint16_t)raw[5] << 8) | raw[6];
    f->seq     = ((uint16_t)raw[7] << 8) | raw[8];
    f->channel = raw[9];
    f->command = ((uint16_t)raw[10] << 8) | raw[11];
    f->arg_len = len - 18;
    if (f->arg_len > 0) {
        f->arg = malloc(f->arg_len);
        if (f->arg) {
            memcpy(f->arg, raw + 12, f->arg_len);
        } else {
            f->arg_len = 0;
        }
    }
    return f;
}

/* ── 状态机逐字节喂入 ─────────────────────────────────────────── */
static void syncFeed(uint8_t byte)
{
    switch (s_syncState) {

    case SYNC_HEAD0:
        if (byte == FRAME_HEAD0) {
            s_frameBuf[0] = byte;
            s_syncState = SYNC_HEAD1;
        }
        break;

    case SYNC_HEAD1:
        if (byte == FRAME_HEAD1) {
            s_frameBuf[1] = byte;
            s_syncState = SYNC_LEN_H;
        } else {
            s_syncState = SYNC_HEAD0;
        }
        break;

    case SYNC_LEN_H:
        s_frameBuf[2] = byte;
        s_frameLen = (uint16_t)byte << 8;
        s_syncState = SYNC_LEN_L;
        break;

    case SYNC_LEN_L:
        s_frameBuf[3] = byte;
        s_frameLen |= byte;
        if (s_frameLen < FRAME_MIN || s_frameLen > FRAME_MAX) {
            ESP_LOGW(TAG, "帧长异常: %u", s_frameLen);
            s_syncState = SYNC_HEAD0;
        } else {
            s_dataPos = 0;
            s_syncState = SYNC_DATA;
        }
        break;

    case SYNC_DATA:
        s_frameBuf[4 + s_dataPos] = byte;
        s_dataPos++;
        if (s_dataPos >= s_frameLen - 4) {
            bcgFrame_t *f = frameParse(s_frameBuf, s_frameLen);
            if (f) {
                if (xQueueSend(s_frameQueue, &f, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "帧队列满，丢弃 cmd=0x%04X", f->command);
                    frameFree(f);
                }
            }
            s_syncState = SYNC_HEAD0;
        }
        break;
    }
}

/* ── 构造并发送帧 ─────────────────────────────────────────────── */
static uint16_t frameSend(uint8_t channel, uint16_t command,
                          const uint8_t *arg, uint16_t arg_len)
{
    uint16_t datafieldLen = 2 + arg_len;
    uint16_t totalLen = 16 + datafieldLen;
    uint16_t seq = s_seq++;

    uint8_t preamble[FRAME_MAX];
    uint16_t off = 0;

    preamble[off++] = FRAME_HEAD0;
    preamble[off++] = FRAME_HEAD1;
    preamble[off++] = (totalLen >> 8) & 0xFF;
    preamble[off++] = totalLen & 0xFF;
    preamble[off++] = FRAME_VER;
    preamble[off++] = 0x01;   /* Type hi: H→D need response */
    preamble[off++] = 0x00;   /* Type lo: normal */
    preamble[off++] = (seq >> 8) & 0xFF;
    preamble[off++] = seq & 0xFF;
    preamble[off++] = channel;
    preamble[off++] = (command >> 8) & 0xFF;
    preamble[off++] = command & 0xFF;
    if (arg_len > 0) {
        memcpy(preamble + off, arg, arg_len);
        off += arg_len;
    }

    uint32_t crc = crc32Calc(preamble, off);

    preamble[off++] = (crc >> 24) & 0xFF;
    preamble[off++] = (crc >> 16) & 0xFF;
    preamble[off++] = (crc >> 8) & 0xFF;
    preamble[off++] = crc & 0xFF;
    preamble[off++] = FRAME_TAIL0;
    preamble[off++] = FRAME_TAIL1;

    uartDriver_write(BCG_UART_NUM, preamble, off);
    return seq;
}

/* ── RX Task ───────────────────────────────────────────────────── */
static void sleepMonitorRxTask(void *arg)
{
    uint8_t buf[256];

    for (;;) {
        int len = uartDriver_read(BCG_UART_NUM, buf, sizeof(buf), 100);

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                syncFeed(buf[i]);
            }
        }
    }
}

/* ── 体征描述字符串 ────────────────────────────────────────────── */

static const char *bedStateStr(uint8_t s)
{
    switch (s) {
    case 0x00: return "离开";
    case 0x01: return "在床无体动";
    case 0x02: return "在床有体动";
    default:   return "未知";
    }
}

static const char *sleepStateStr(uint8_t s)
{
    switch (s) {
    case 0x00: return "觉醒";
    case 0x01: return "浅睡";
    case 0x02: return "深睡";
    case 0x03: return "快速眼动";
    case 0x04: return "离床";
    default:   return "未知";
    }
}

static const char *fatigueStr(uint8_t v)
{
    if (v == 0) return "离床";
    if (v >= 36) return "清醒/正常";
    if (v >= 20) return "轻度疲劳";
    return "重度疲劳";
}

static const char *breathHoldStr(uint8_t v)
{
    switch (v) {
    case 0x00: return "正常呼吸";
    case 0x01: return "憋气";
    case 0x04: return "离床";
    default:   return "未知";
    }
}

static const char *stressStr(uint16_t v)
{
    if (v < 50)   return "放松";
    if (v <= 200) return "中等应激";
    return "高度应激";
}

/* ── 自动上报帧分发 ────────────────────────────────────────────── */

static void dispatchReport(const bcgFrame_t *f)
{
    switch (f->command) {

    case 0x0202:   /* HR/RR/在离床/体动 */
        if (f->arg_len >= 4) {
            uint8_t bed = f->arg[3];
            uint8_t person = (bed != 0x00) ? 1 : 0;
            uint8_t move   = (bed == 0x02) ? 1 : 0;
            ESP_LOGI(TAG, "[%lu][上报] channel=%u HR=%u RR=%u bed=%s",
                     esp_log_timestamp(), f->channel,
                     f->arg[1], f->arg[2], bedStateStr(bed));
            trans2cloud_updateBcg(person, f->arg[2], f->arg[1], move);
        }
        break;

    case 0x020B:   /* 睡眠/疲劳/憋气/压力 */
        if (f->arg_len >= 7) {
            uint16_t stress = ((uint16_t)f->arg[4] << 8) | f->arg[5];
            ESP_LOGI(TAG, "[%lu][上报] channel=%u sleep=%s fatigue=%u(%s) "
                     "breath=%s stress=%u(%s) bed=%s",
                     esp_log_timestamp(), f->channel,
                     sleepStateStr(f->arg[1]), f->arg[2], fatigueStr(f->arg[2]),
                     breathHoldStr(f->arg[3]), stress, stressStr(stress),
                     bedStateStr(f->arg[6]));
            trans2cloud_updateBcgVital(f->arg[1], f->arg[2], f->arg[3], stress);
        }
        break;

    case 0x0205:   /* AD 采样数据 */
        if (f->arg_len >= 1007) {
            uint32_t sTime = ((uint32_t)f->arg[1] << 24) |
                             ((uint32_t)f->arg[2] << 16) |
                             ((uint32_t)f->arg[3] << 8) |
                             f->arg[4];
            int cnt = (f->arg_len - 7) / 2;
            int16_t s0 = ((int16_t)f->arg[7]  << 8) | f->arg[8];
            int16_t s1 = ((int16_t)f->arg[9]  << 8) | f->arg[10];
            int16_t s2 = ((int16_t)f->arg[11] << 8) | f->arg[12];
            int16_t s3 = ((int16_t)f->arg[13] << 8) | f->arg[14];
            int16_t s4 = ((int16_t)f->arg[15] << 8) | f->arg[16];
            int tail = 7 + cnt * 2;
            int16_t t4 = ((int16_t)f->arg[tail-10] << 8) | f->arg[tail-9];
            int16_t t3 = ((int16_t)f->arg[tail-8]  << 8) | f->arg[tail-7];
            int16_t t2 = ((int16_t)f->arg[tail-6]  << 8) | f->arg[tail-5];
            int16_t t1 = ((int16_t)f->arg[tail-4]  << 8) | f->arg[tail-3];
            int16_t t0 = ((int16_t)f->arg[tail-2]  << 8) | f->arg[tail-1];
            // ESP_LOGI(TAG, "[%lu][上报] channel=%u time=%lu samples=%d "
            //          "前5=[%d,%d,%d,%d,%d] 后5=[%d,%d,%d,%d,%d]",
            //          esp_log_timestamp(), f->channel, sTime, cnt,
            //          s0, s1, s2, s3, s4, t4, t3, t2, t1, t0);
        }
        break;

    default:
        ESP_LOGI(TAG, "[%lu][上报] channel=%u cmd=0x%04X arg_len=%u",
                 esp_log_timestamp(), f->channel, f->command, f->arg_len);
        break;
    }
}

/* ── 等待应答帧（从队列消费，跳过上报帧）──────────────────────── */

static bcgFrame_t *waitResponse(uint16_t command, uint32_t timeoutMs)
{
    TickType_t start = xTaskGetTickCount();

    for (;;) {
        TickType_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed >= timeoutMs) return NULL;

        TickType_t remain = pdMS_TO_TICKS(timeoutMs - elapsed);
        bcgFrame_t *f = NULL;
        if (xQueueReceive(s_frameQueue, &f, remain) != pdTRUE) return NULL;

        uint8_t typeHi = (f->type >> 8) & 0xFF;

        if (typeHi == 0x02 && f->command == command) {
            return f;
        }

        /* 非匹配帧：上报帧打印 */
        if (typeHi == 0x06) {
            dispatchReport(f);
        }
        frameFree(f);
    }
}

/* ── 通用：发送查询 → 等待应答 ─────────────────────────────────── */

static esp_err_t sendQuery(uint8_t channel, uint16_t command,
                           const uint8_t *arg, uint16_t argLen,
                           bcgFrame_t **out)
{
    frameSend(channel, command, arg, argLen);
    *out = waitResponse(command, 3000);
    if (!*out) {
        ESP_LOGW(TAG, "[%lu] cmd=0x%04X ch=%u 查询超时",
                 esp_log_timestamp(), command, channel);
        return ESP_ERR_TIMEOUT;
    }
    if ((*out)->arg_len >= 1 && (*out)->arg[0] != 0) {
        ESP_LOGE(TAG, "[%lu] cmd=0x%04X 设备错误码=%u",
                 esp_log_timestamp(), command, (*out)->arg[0]);
    }
    return ESP_OK;
}

/* ── 自动上报轮询任务 ──────────────────────────────────────────── */

static void sleepMonitorPollTask(void *arg)
{
    while (1) {
        sleepMonitor_pollRx();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * 公开 API
 * ═══════════════════════════════════════════════════════════════════ */

esp_err_t sleepMonitor_init(void)
{
    s_frameQueue = xQueueCreate(16, sizeof(bcgFrame_t *));
    if (!s_frameQueue) {
        ESP_LOGE(TAG, "帧队列创建失败");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = uartDriver_init(BCG_UART_NUM, BCG_TX_PIN,
                                    BCG_RX_PIN, BCG_BAUD_RATE,
                                    BCG_RX_BUF_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BCG UART 初始化失败");
        vQueueDelete(s_frameQueue);
        return ret;
    }

    crc32Init();

    xTaskCreate(sleepMonitorRxTask, "bcg_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "BCG 睡眠监护仪已启动 UART%u TX=IO%d RX=IO%d",
             BCG_UART_NUM, BCG_TX_PIN, BCG_RX_PIN);
    return ESP_OK;
}

esp_err_t sleepMonitor_querySN(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0101, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    /* SN 为二进制数据，按 hex 打印 */
    if (f->arg_len > 1) {
        ESP_LOGI(TAG, "[%lu] channel=%u SN=", esp_log_timestamp(), channel);
        ESP_LOG_BUFFER_HEX(TAG, f->arg + 1, f->arg_len - 1);
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryHrRr(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0200, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 4) {
        ESP_LOGI(TAG, "[%lu] channel=%u HR=%u RR=%u bed=%s",
                 esp_log_timestamp(), channel,
                 f->arg[1], f->arg[2], bedStateStr(f->arg[3]));
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_querySleepState(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0206, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 2) {
        ESP_LOGI(TAG, "[%lu] channel=%u state=%s",
                 esp_log_timestamp(), channel, sleepStateStr(f->arg[1]));
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryFatigue(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0207, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 2) {
        ESP_LOGI(TAG, "[%lu] channel=%u value=%u -> %s",
                 esp_log_timestamp(), channel, f->arg[1], fatigueStr(f->arg[1]));
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryBreathHold(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0208, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 2) {
        ESP_LOGI(TAG, "[%lu] channel=%u state=%s",
                 esp_log_timestamp(), channel, breathHoldStr(f->arg[1]));
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryStress(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0209, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 4) {
        uint16_t stress = ((uint16_t)f->arg[1] << 8) | f->arg[2];
        ESP_LOGI(TAG, "[%lu] channel=%u value=%u(%s) bed=%s",
                 esp_log_timestamp(), channel,
                 stress, stressStr(stress), bedStateStr(f->arg[3]));
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryAdData(uint8_t channel)
{
    uint8_t arg = 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0203, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 1007) {
        uint32_t sTime = ((uint32_t)f->arg[1] << 24) |
                         ((uint32_t)f->arg[2] << 16) |
                         ((uint32_t)f->arg[3] << 8) |
                         f->arg[4];
        int cnt = (f->arg_len - 7) / 2;
        int16_t s0 = ((int16_t)f->arg[7]  << 8) | f->arg[8];
        int16_t s1 = ((int16_t)f->arg[9]  << 8) | f->arg[10];
        int16_t s2 = ((int16_t)f->arg[11] << 8) | f->arg[12];
        int16_t s3 = ((int16_t)f->arg[13] << 8) | f->arg[14];
        int16_t s4 = ((int16_t)f->arg[15] << 8) | f->arg[16];
        int tail = 7 + cnt * 2;
        int16_t t4 = ((int16_t)f->arg[tail-10] << 8) | f->arg[tail-9];
        int16_t t3 = ((int16_t)f->arg[tail-8]  << 8) | f->arg[tail-7];
        int16_t t2 = ((int16_t)f->arg[tail-6]  << 8) | f->arg[tail-5];
        int16_t t1 = ((int16_t)f->arg[tail-4]  << 8) | f->arg[tail-3];
        int16_t t0 = ((int16_t)f->arg[tail-2]  << 8) | f->arg[tail-1];
        ESP_LOGI(TAG, "[%lu] channel=%u time=%lu samples[0..4]=%d,%d,%d,%d,%d "
                 "...[%d..%d]=%d,%d,%d,%d,%d",
                 esp_log_timestamp(), channel, sTime,
                 s0, s1, s2, s3, s4, cnt - 5, cnt - 1, t4, t3, t2, t1, t0);
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_queryAll(uint8_t channel)
{
    sleepMonitor_queryHrRr(channel);
    sleepMonitor_querySleepState(channel);
    sleepMonitor_queryFatigue(channel);
    sleepMonitor_queryBreathHold(channel);
    sleepMonitor_queryStress(channel);
    sleepMonitor_queryAdData(channel);
    return ESP_OK;
}

esp_err_t sleepMonitor_autoReportHrRr(uint8_t channel, bool enable)
{
    uint8_t arg = enable ? 0x01 : 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0201, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 1 && f->arg[0] == 0) {
        ESP_LOGI(TAG, "[%lu] HR/RR 自动上报: %s",
                 esp_log_timestamp(), enable ? "开启" : "关闭");
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_autoReportVital(uint8_t channel, bool enable)
{
    uint8_t arg = enable ? 0x01 : 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x020A, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 1 && f->arg[0] == 0) {
        ESP_LOGI(TAG, "[%lu] 睡眠/疲劳/憋气/压力 自动上报: %s",
                 esp_log_timestamp(), enable ? "开启" : "关闭");
    }
    frameFree(f);
    return ESP_OK;
}

esp_err_t sleepMonitor_autoReportAd(uint8_t channel, bool enable)
{
    uint8_t arg = enable ? 0x01 : 0x00;
    bcgFrame_t *f = NULL;
    esp_err_t ret = sendQuery(channel, 0x0204, &arg, 1, &f);
    if (ret != ESP_OK) return ret;

    if (f->arg_len >= 1 && f->arg[0] == 0) {
        ESP_LOGI(TAG, "[%lu] AD 采样自动上报: %s",
                 esp_log_timestamp(), enable ? "开启" : "关闭");
    }
    frameFree(f);
    return ESP_OK;
}

void sleepMonitor_setPollingMode(void)
{
    ESP_LOGI(TAG, "[%lu] 切换到轮询模式", esp_log_timestamp());
    if (s_pollTask) {
        vTaskDelete(s_pollTask);
        s_pollTask = NULL;
    }
    sleepMonitor_autoReportHrRr(1, false);
    sleepMonitor_autoReportVital(1, false);
    sleepMonitor_autoReportAd(1, false);
}

void sleepMonitor_setAutoReportMode(void)
{
    ESP_LOGI(TAG, "[%lu] 切换到自动上报模式", esp_log_timestamp());
    sleepMonitor_autoReportAd(1, true);
    sleepMonitor_autoReportVital(1, true);
    sleepMonitor_autoReportHrRr(1, true);
    if (!s_pollTask) {
        xTaskCreate(sleepMonitorPollTask, "bcg_poll", 2048, NULL, 3, &s_pollTask);
    }
}

void sleepMonitor_pollRx(void)
{
    bcgFrame_t *f = NULL;
    if (xQueueReceive(s_frameQueue, &f, 0) == pdTRUE) {
        uint8_t typeHi = (f->type >> 8) & 0xFF;
        if (typeHi == 0x06) {
            dispatchReport(f);
        }
        frameFree(f);
    }
}
