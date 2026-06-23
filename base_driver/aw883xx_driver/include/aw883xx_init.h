/*
 * aw883xx_init.h — 初始化信息结构体 (adapted from AWINIC aw883xx_init.h)
 *
 * 简化版：单设备单场景，移除 mix_chip_count / phase_sync / fade_en / cali_check_st。
 */
#ifndef __AW883XX_INIT_H__
#define __AW883XX_INIT_H__

#include "aw883xx_base.h"
#include "aw_profile_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AW883xx 设备初始化信息
 *
 * 应用程序填充此结构体，然后调用 aw883xx_smartpa_init()。
 * i2c_read_func / i2c_write_func 由 aw883xx_driver.c 内部适配层实现，
 * reset_gpio_ctl 由 ESP32 GPIO 驱动实现。
 */
struct aw_init_info {
    aw_dev_index_t dev;           /* 设备编号 (AW_DEV_0) */
    unsigned char  i2c_addr;      /* 7-bit I2C 地址 (0x34) */
    unsigned int   re_min;        /* 喇叭直流阻抗最小值（毫欧，默认 2000） */
    unsigned int   re_max;        /* 喇叭直流阻抗最大值（毫欧，默认 39000） */

    struct aw_prof_info *prof_info; /* 场景配置数据指针 */

    /* ─── 平台抽象函数指针 ─── */
    int  (*i2c_read_func)(uint16_t dev_addr, uint8_t reg_addr,
                           uint8_t *pdata, uint16_t len);
    int  (*i2c_write_func)(uint16_t dev_addr, uint8_t reg_addr,
                            uint8_t *pdata, uint16_t len);
    void (*reset_gpio_ctl)(bool state);

    /**
     * 芯片特定设备初始化函数（PID_2183 = aw883xx_pid_2183_dev_init）
     * 该函数填充 aw_device 寄存器描述符和 ops 函数指针表。
     */
    int  (*dev_init_ops)(void *aw883xx);
};

#ifdef __cplusplus
}
#endif

#endif /* __AW883XX_INIT_H__ */
