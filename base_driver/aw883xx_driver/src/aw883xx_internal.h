/*
 * aw883xx_internal.h — 驱动内部共享结构体
 *
 * struct aw883xx_driver 的完整定义，供：
 *   - aw883xx_driver.c (主驱动)
 *   - aw883xx_pid_2183_init.c (设备初始化)
 * 共同使用。
 */
#ifndef __AW883XX_INTERNAL_H__
#define __AW883XX_INTERNAL_H__

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "aw883xx_base.h"
#include "aw883xx_device.h"
#include "aw883xx_init.h"

/**
 * @brief AW883xx 驱动主结构体（全局单例）
 */
struct aw883xx_driver {
    i2c_master_dev_handle_t i2c_dev;      /* I2C 设备句柄 */
    gpio_num_t              reset_pin;    /* RSTN 引脚 */
    uint32_t                chip_id;      /* CHIPID (0x2183) */
    aw_dev_index_t          dev_index;    /* 设备编号 (AW_DEV_0) */
    uint8_t                 i2c_addr;     /* 7-bit I2C 地址 (0x34) */
    bool                    inited;       /* 初始化完成标志 */

    struct aw_device       *aw_pa;        /* 设备描述符 */
    struct aw_init_info     init_info;    /* 初始化信息 */
};

/* ─── pid_2183 设备初始化（定义于 aw883xx_pid_2183_init.c）─── */
int aw883xx_pid_2183_dev_init(struct aw883xx_driver *drv);

#endif /* __AW883XX_INTERNAL_H__ */
