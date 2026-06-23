/*
 * aw883xx_base.h — ESP32 平台抽象层 (adapted from AWINIC aw883xx_base.h)
 *
 * 将 STM32 HAL 平台依赖映射为 ESP-IDF API：
 *   - HAL_Delay()     → vTaskDelay()
 *   - printf          → ESP_LOGx
 *   - aw_mutex_lock   → (单任务调用，无需锁)
 */
#ifndef __AW883XX_BASE_H__
#define __AW883XX_BASE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ═══════════════════════════════════════════════════════════════
 * 功能裁剪控制
 * ═══════════════════════════════════════════════════════════════ */
/* #define AW_DEBUG */
/* #define AW_FADE */
/* #define AW_VOLUME */
/* #define AW_IRQ */
/* #define AW_MONITOR */
/* #define AW_CALIB */

/* ═══════════════════════════════════════════════════════════════
 * 延时
 * ═══════════════════════════════════════════════════════════════ */
#define AW_MS_DELAY(ms)  vTaskDelay(pdMS_TO_TICKS(ms))

/* ═══════════════════════════════════════════════════════════════
 * 日志（映射到 ESP_LOG）
 * ═══════════════════════════════════════════════════════════════ */
#define AWINIC_ERR_LOG
#define AWINIC_INFO_LOG
/* #define AWINIC_DEBUG_LOG */

#ifdef AWINIC_ERR_LOG
#define aw_dev_err(dev_index, format, ...) \
    ESP_LOGE("aw883xx", "[d%d]%s: " format, (int)(dev_index), __func__, ##__VA_ARGS__)
#else
#define aw_dev_err(dev_index, format, ...)
#endif

#ifdef AWINIC_INFO_LOG
#define aw_dev_info(dev_index, format, ...) \
    ESP_LOGI("aw883xx", "[d%d]%s: " format, (int)(dev_index), __func__, ##__VA_ARGS__)
#else
#define aw_dev_info(dev_index, format, ...)
#endif

#ifdef AWINIC_DEBUG_LOG
#define aw_dev_dbg(dev_index, format, ...) \
    ESP_LOGD("aw883xx", "[d%d]%s: " format, (int)(dev_index), __func__, ##__VA_ARGS__)
#else
#define aw_dev_dbg(dev_index, format, ...)
#endif

#ifdef AWINIC_ERR_LOG
#define aw_pr_err(format, ...)  ESP_LOGE("aw883xx", "%s: " format, __func__, ##__VA_ARGS__)
#else
#define aw_pr_err(format, ...)
#endif

#ifdef AWINIC_INFO_LOG
#define aw_pr_info(format, ...) ESP_LOGI("aw883xx", "%s: " format, __func__, ##__VA_ARGS__)
#else
#define aw_pr_info(format, ...)
#endif

#ifdef AWINIC_DEBUG_LOG
#define aw_pr_dbg(format, ...)  ESP_LOGD("aw883xx", "%s: " format, __func__, ##__VA_ARGS__)
#else
#define aw_pr_dbg(format, ...)
#endif

/* ═══════════════════════════════════════════════════════════════
 * 互斥锁（单任务调用 — 不需要）
 * ═══════════════════════════════════════════════════════════════ */
#define aw_mutex_lock()   do {} while (0)
#define aw_mutex_unlock() do {} while (0)

/* ═══════════════════════════════════════════════════════════════
 * 共享枚举
 * ═══════════════════════════════════════════════════════════════ */
typedef enum {
    AW_DEV_0 = 0,
    AW_DEV_1,
    AW_DEV_2,
    AW_DEV_3,
    AW_DEV_MAX = 4,
} aw_dev_index_t;

typedef enum {
    AW_PHASE_SYNC_DISABLE = 0,
    AW_PHASE_SYNC_ENABLE,
} aw_phase_sync_t;

typedef enum {
    AW_FADE_DISABLE = 0,
    AW_FADE_ENABLE,
} aw_fade_en_t;

typedef enum {
    NOT_SINGLE = 0,
    IS_SINGLE,
} aw_single_t;

typedef enum {
    AW_RECORD_SEC_DATA = 0,
    AW_RECOVERY_SEC_DATA = 1,
} backup_sec_t;

typedef enum {
    AW_GET_DEV_PARAMS = 0,
    AW_SET_DEV_PARAMS = 1,
} params_option_t;

typedef enum {
    AW_GET_DEV_STATUS = 0,
    AW_SET_DEV_STATUS = 1,
} status_option_t;

typedef enum {
    AW_DEV_NONE_MSG = 0,
    AW_DEV_DSP_PARAMS,
    AW_DEV_HMUTE_PARAMS,
    AW_DEV_INT_PARAMS,
    AW_DEV_VOLUME_PARAMS,
    AW_DEV_FADE_STEP_PARAMS,
    AW_DEV_FADE_IN_TIME_PARAMS,
    AW_DEV_FADE_OUT_TIME_PARAMS,
    AW_DEV_CRC_FLAG_PARAMS,
    AW_DEV_SYSST_PARAMS,
    AW_DEV_DSP_RE_PARAMS,
    AW_DEV_BIN_RE_PARAMS,
    AW_DEV_TE_PARAMS,
    AW_DEV_F0_PARAMS,
    AW_DEV_R0_PARAMS,
    AW_DEV_RE_RANGE_PARAMS,
    AW_DEV_CALI_RE_PARAMS,
    AW_DEV_CALI_F0_PARAMS,
    AW_DEV_CALI_Q_PARAMS,
    AW_DEV_CALI_RE_F0_PARAMS,
    AW_DEV_CALI_F0_Q_PARAMS,
    AW_DEV_CALI_TIME_PARAMS,
    AW_DEV_STORE_CALI_RE_PARAMS,
    AW_DEV_REALTIME_CRC_GET_PARAMS,
    AW_DEV_REALTIME_CRC_SET_PARAMS,
} dev_params_t;

typedef enum {
    AW_DEV_NONE_STATUS = 0,
    AW_DEV_PLL_WDT_STATUS,
    AW_DEV_CLEAR_INT_STATUS,
    AW_DEV_INTERRUPT_CLEAR_STATUS,
    AW_DEV_SOFT_RESET_STATUS,
    AW_DEV_REG_DUMP_STATUS,
} dev_status_t;

struct cali_msg_hdr {
    uint32_t opcode_id;
    uint32_t msg[AW_DEV_MAX];
};

/* ═══════════════════════════════════════════════════════════════
 * 错误码（与 Linux errno 保持一致）
 * ═══════════════════════════════════════════════════════════════ */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define E2BIG    7
#define ENOEXEC  8
#define EBADF    9
#define ECHILD   10
#define EAGAIN   11
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define ENOTBLK  15
#define EBUSY    16
#define EEXIST   17
#define EXDEV    18
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOTTY   25
#define EFBIG    27
#define ENOSPC   28
#define ESPIPE   29
#define EROFS    30
#define EMLINK   31
#define EPIPE    32
#define EDOM     33
#define ERANGE   34

#endif /* __AW883XX_BASE_H__ */
