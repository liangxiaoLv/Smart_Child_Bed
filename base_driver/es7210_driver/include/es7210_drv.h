#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ES7210 MIC 通道选择 (与 esp_codec_dev 兼容) ─────────────── */
#define ES7210_DRV_SEL_MIC1   (1 << 0)
#define ES7210_DRV_SEL_MIC2   (1 << 1)
#define ES7210_DRV_SEL_MIC3   (1 << 2)
#define ES7210_DRV_SEL_MIC4   (1 << 3)

typedef void *es7210_drv_handle_t;

typedef struct {
    i2c_master_bus_handle_t i2c_bus;      /* I2C 总线句柄 */
    int mclk_io;                          /* MCLK 引脚 */
    int bclk_io;                          /* BCLK 引脚 */
    int ws_io;                            /* LRCK/WS 引脚 */
    int din_io;                           /* DIN 引脚 (接 ES7210 SDOUT) */
    uint32_t sample_rate;                 /* 采样率 (Hz), 如 16000 */
    uint8_t  mic_mask;                    /* 麦克风通道掩码 */
    uint8_t  pga_gain;                    /* 模拟 PGA 增益 (0~14, 每档 3dB) */
    uint8_t  total_slots;                 /* I2S 总 slot 数 (2=STD立体声, 4=TDM四通道) */
} es7210_drv_config_t;

/**
 * @brief  初始化 ES7210 + I2S 并上电
 *
 * 时序: I2S 使能(产生 MCLK) → I2C 配置 ES7210 寄存器 → 上电
 *
 * @param cfg    配置参数
 * @param handle 输出句柄
 * @return ESP_OK 成功, 其他为错误码
 */
esp_err_t es7210_drv_init(const es7210_drv_config_t *cfg, es7210_drv_handle_t *handle);

/**
 * @brief  读取 PCM 采样数据
 *
 * @param handle  句柄
 * @param buf     输出缓冲区 (int16_t, TDM: slot0,slot1,...交错)
 * @param samples 期望读取的帧数 (每帧 = total_slots 个采样)
 * @return 实际读取的帧数, <0 表示错误
 */
int es7210_drv_read(es7210_drv_handle_t handle, int16_t *buf, int samples);

/**
 * @brief  打印 ES7210 全部寄存器值 (调试用)
 */
void es7210_drv_dump_regs(es7210_drv_handle_t handle);

/**
 * @brief  下电并释放 ES7210 + I2S 资源
 */
esp_err_t es7210_drv_deinit(es7210_drv_handle_t handle);

#ifdef __cplusplus
}
#endif
