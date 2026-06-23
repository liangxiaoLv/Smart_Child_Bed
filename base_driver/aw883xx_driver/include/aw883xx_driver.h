/*
 * aw883xx_driver.h — AW883xx 音频功放 ESP32 驱动 API
 *
 * 适配自 AWINIC aw883xx.h，支持：
 *   - DSP 固件加载与场景切换（Music / Voice / Receiver）
 *   - 硬件复位 + CHIPID 校验
 *   - 音量控制（SYSCTRL2 寄存器）
 *   - 采样率配置（I2SCTRL1 PLL 重锁）
 *   - 诊断输出（寄存器 + DSP 状态 + 固件版本）
 *
 * 使用流程:
 *   1. aw883xx_init(bus)          // I2C 总线初始化 + 固件加载
 *   2. [可选] aw883xx_setProfile("Receiver") // 切换场景
 *   3. aw883xx_start()            // 上电 → PLL → DSP → 出音
 *   4. aw883xx_setVolume(80)      // 播放期间调整音量
 *   5. aw883xx_stop()             // 静音 → 下电
 *   6. aw883xx_deinit()           // 释放资源
 */
#ifndef __AW883XX_DRIVER_H__
#define __AW883XX_DRIVER_H__

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════
 * AW883xx 状态枚举
 * ═══════════════════════════════════════════════════════════════ */
typedef enum {
    AW_START = 0,
    AW_STOP  = 1,
} aw_ctrl_t;

typedef enum {
    AW_DSP_WORK   = 0,
    AW_DSP_BYPASS = 1,
} aw_dsp_ctrl_t;

/* ═══════════════════════════════════════════════════════════════
 * 生命周期 API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 初始化 AW883xx 功放
 *
 * 执行: HW 复位 → CHIPID 校验 (0x2183) → 寄存器描述符填充
 *       → 固件加载 (reg + DSP FW + DSP CFG) → 默认场景 "Music"
 *
 * @param bus  I2C 总线句柄 (I2C0)
 * @return ESP_OK 成功，其他 失败（I2C 通信错误 / CHIPID 不匹配）
 */
esp_err_t aw883xx_init(i2c_master_bus_handle_t bus);

/**
 * @brief 释放 AW883xx 资源
 *
 * 先调用 aw883xx_stop() 安全关闭，再释放内存和 I2C 设备句柄。
 *
 * @return ESP_OK 成功
 */
esp_err_t aw883xx_deinit(void);

/* ═══════════════════════════════════════════════════════════════
 * 播放控制 API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 启动功放输出
 *
 * 顺序：清 PWDN → 等 PLL 锁定 → 清 AMPPD → 等 SWS → 清 DSPBY → 清 HMUTE
 * 调用前需确保 ESP32 I2S 已在输出 BCLK/LRCK。
 *
 * @return ESP_OK 成功
 */
esp_err_t aw883xx_start(void);

/**
 * @brief 停止功放输出
 *
 * 顺序：HMUTE → 关 I2S TX → 中断全屏蔽 → DSPBY → AMPPD → PWDN
 *
 * @return ESP_OK 成功
 */
esp_err_t aw883xx_stop(void);

/* ═══════════════════════════════════════════════════════════════
 * 参数控制 API
 * ═══════════════════════════════════════════════════════════════ */

/**
 * @brief 设置音量
 *
 * 0 = 静音 (VOL=0x3FF)，100 = 最大 (VOL=0)
 * 内部映射：reg_vol = (uint16_t)((100-pct) * 1023 / 100)
 *
 * @param pct  0 ~ 100
 * @return ESP_OK 成功
 */
esp_err_t aw883xx_setVolume(uint8_t pct);

/**
 * @brief 同步 I2S 采样率
 *
 * 写入 I2SCTRL1[3:0] (I2SSR)，触发 PLL 重新锁定。
 * 支持: 8000/11025/12000/16000/22050/24000/32000/44100/48000
 *
 * @param sample_rate  I2S 采样率 (Hz)
 * @return ESP_OK / ESP_ERR_NOT_SUPPORTED
 */
esp_err_t aw883xx_setSampleRate(uint32_t sample_rate);

/**
 * @brief 切换音频场景
 *
 * 从 aw_params.h 加载对应场景的寄存器 + DSP 配置。
 * 如果当前正在播放，会先停止再重新启动。
 *
 * @param name  场景名 ("Music", "Receiver")
 * @return ESP_OK / ESP_ERR_NOT_FOUND
 */
esp_err_t aw883xx_setProfile(const char *name);

/**
 * @brief 获取当前场景名
 *
 * @return 场景名字符串 ("Music" / "Receiver")
 */
const char *aw883xx_getProfile(void);

/* ═══════════════════════════════════════════════════════════════
 * 查询 API
 * ═══════════════════════════════════════════════════════════════ */

/** @brief 是否已初始化 */
bool aw883xx_isInited(void);

/**
 * @brief 读取固件版本号
 *
 * @param ver  [输出] 固件版本（16-bit DSP 寄存器）
 * @return ESP_OK 成功
 */
esp_err_t aw883xx_getFwVersion(uint32_t *ver);

/**
 * @brief 诊断输出（寄存器 + DSP 状态 + 固件版本 + 场景名）
 *
 * 通过 ESP_LOGI 输出到串口，用于调试。
 */
void aw883xx_testWrites(void);

#ifdef __cplusplus
}
#endif

#endif /* __AW883XX_DRIVER_H__ */
