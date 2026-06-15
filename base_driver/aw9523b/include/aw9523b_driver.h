#pragma once

#include "pin_map.h"
#include "i2c_driver.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* ── 寄存器地址 (AW9523B) ─────────────────────────────────── */
#define AW9523B_REG_ID          0x10  /* 芯片 ID, 读出 0x23 */
#define AW_REG_INPUT_P0         0x00  /* P0 输入状态 (只读) */
#define AW_REG_INPUT_P1         0x01  /* P1 输入状态 (只读) */
#define AW_REG_OUTPUT_P0        0x02  /* P0 输出寄存器 */
#define AW_REG_OUTPUT_P1        0x03  /* P1 输出寄存器 */
#define AW_REG_DIR_P0           0x04  /* P0 方向: 1=输入 0=输出 */
#define AW_REG_DIR_P1           0x05  /* P1 方向: 1=输入 0=输出 */
#define AW_REG_INT_EN_P0        0x06  /* P0 中断使能 (1=允许中断) */
#define AW_REG_INT_EN_P1        0x07  /* P1 中断使能 */

/* ── 16 个 IO 位掩码 (P0.0~P0.7, P1.0~P1.7) ───────────────── */
#define AW_PIN_P00     (1 << 0) /*LED，低电平点亮*/
#define AW_PIN_P01     (1 << 1)
#define AW_PIN_P02     (1 << 2)
#define AW_PIN_P03     (1 << 3)
#define AW_PIN_P04     (1 << 4)
#define AW_PIN_P05     (1 << 5)
#define AW_PIN_P06     (1 << 6)
#define AW_PIN_P07     (1 << 7)
#define AW_PIN_P10     (1 << 8)   /* 跨端口用 16-bit 掩码 */
#define AW_PIN_P11     (1 << 9)
#define AW_PIN_P12     (1 << 10)
#define AW_PIN_P13     (1 << 11)
#define AW_PIN_P14     (1 << 12)
#define AW_PIN_P15     (1 << 13)
#define AW_PIN_P16     (1 << 14)
#define AW_PIN_P17     (1 << 15)

/* 端口枚举 */
typedef enum {
    AW9523B_PORT0 = 0,  /* P0.0~P0.7 */
    AW9523B_PORT1 = 1,  /* P1.0~P1.7 */
} aw9523b_port_t;

/* 方向 / 电平语义宏 (替代 true/false, 提升可读性) */
#define AW_PIN_IN       true    /* 设为输入 */
#define AW_PIN_OUT      false   /* 设为输出 */
#define AW_PIN_HIGH     true    /* 输出高电平 */
#define AW_PIN_LOW      false   /* 输出低电平 */

/* 中断触发回调类型: 传入 16-bit 输入电平快照 */
typedef void (*aw9523b_isr_t)(uint16_t pin_levels);

/**
 * 初始化 AW9523B
 * - 硬件复位 (拉低 RSTN 1ms)
 * - 校验芯片 ID = 0x23
 * - 配置 16 个 IO 全部为输入
 * - 启动 INTN 中断 (下降沿)
 *
 * @param bus  I2C 总线句柄
 * @param cb   中断回调 (NULL = 不启用中断)
 */
esp_err_t aw9523bDriver_init(i2c_master_bus_handle_t bus, aw9523b_isr_t cb);

/**
 * 读 16 位输入电平快照 (P0 低 8 位, P1 高 8 位)
 */
esp_err_t aw9523bDriver_readAll(uint16_t *levels);

/**
 * 写整个端口输出
 */
esp_err_t aw9523bDriver_writePort(aw9523b_port_t port, uint8_t value);

/**
 * 读整个端口输入
 */
esp_err_t aw9523bDriver_readPort(aw9523b_port_t port, uint8_t *value);

/**
 * 设置单个引脚方向 (1=输入, 0=输出)
 * @param pin_mask  16-bit 掩码 (如 AW_PIN_P00 | AW_PIN_P10)
 */
esp_err_t aw9523bDriver_setDir(uint16_t pin_mask, bool input);

/**
 * 设置单个引脚输出电平
 */
esp_err_t aw9523bDriver_setPin(uint16_t pin_mask, bool level);
