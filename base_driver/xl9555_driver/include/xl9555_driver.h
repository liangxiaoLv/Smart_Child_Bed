#pragma once

#include "pin_map.h"    /* XL9555_I2C_ADDR 等硬件地址来自此处 */
#include "i2c_driver.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/* ── 寄存器地址 ──────────────────────────────────────────── */
#define XL9555_REG_INPUT0   0x00  /* Port-0 输入 */
#define XL9555_REG_INPUT1   0x01  /* Port-1 输入 */
#define XL9555_REG_OUTPUT0  0x02  /* Port-0 输出 */
#define XL9555_REG_OUTPUT1  0x03  /* Port-1 输出 */
#define XL9555_REG_CFG0     0x06  /* Port-0 方向（1=输入） */
#define XL9555_REG_CFG1     0x07  /* Port-1 方向（1=输入） */

/* ── 引脚位掩码（Port-0） ────────────────────────────────── */
#define XL9555_AP_INT       (1 << 0)   /* P0.0  IN  AP 中断 */
#define XL9555_QMA_INT      (1 << 1)   /* P0.1  IN  QMA 中断 */
#define XL9555_SPK_EN       (1 << 2)   /* P0.2  OUT 扬声器使能（H=关） */
#define XL9555_BEEP         (1 << 3)   /* P0.3  OUT 蜂鸣器（H=关） */
#define XL9555_OV_PWDN      (1 << 4)   /* P0.4  OUT OV 掉电 */
#define XL9555_OV_RESET     (1 << 5)   /* P0.5  OUT OV 复位 */
#define XL9555_GBC_LED      (1 << 6)   /* P0.6  OUT GBC LED */
#define XL9555_GBC_KEY      (1 << 7)   /* P0.7  IN  GBC 按键 */

/* ── 引脚位掩码（Port-1） ────────────────────────────────── */
#define XL9555_LCD_BL       (1 << 0)   /* P1.0  OUT LCD 背光 */
#define XL9555_CT_RST       (1 << 1)   /* P1.1  OUT 触摸复位 */
#define XL9555_SLCD_RST     (1 << 2)   /* P1.2  OUT LCD 复位 */
#define XL9555_SLCD_PWR     (1 << 3)   /* P1.3  OUT LCD 电源 */
#define XL9555_KEY3         (1 << 4)   /* P1.4  IN  按键3 */
#define XL9555_KEY2         (1 << 5)   /* P1.5  IN  按键2 */
#define XL9555_KEY1         (1 << 6)   /* P1.6  IN  按键1 */
#define XL9555_KEY0         (1 << 7)   /* P1.7  IN  按键0 */

/* ── 端口枚举 ────────────────────────────────────────────── */
typedef enum {
    XL9555_PORT0 = 0,
    XL9555_PORT1 = 1,
} xl9555_port_t;

/**
 * 初始化 XL9555。
 *
 * @param bus  已初始化的 I2C 总线句柄（由 main.c 统一创建）
 *
 * 按 pin_map.h 配置方向寄存器：
 *   Port-0: 0x03（P0.0/P0.1 输入，其余输出）
 *   Port-1: 0xF0（P1.7-P1.4 输入，其余输出）
 * 初始输出：SPK_EN / BEEP 拉高（关闭）。
 */
esp_err_t xl9555Driver_init(i2c_master_bus_handle_t bus);

/**
 * 写整个端口的输出寄存器。
 * @param port   XL9555_PORT0 或 XL9555_PORT1
 * @param value  8-bit 输出值
 */
esp_err_t xl9555Driver_writePort(xl9555_port_t port, uint8_t value);

/**
 * 读整个端口的输入寄存器。
 * @param port    XL9555_PORT0 或 XL9555_PORT1
 * @param value   输出读取结果
 */
esp_err_t xl9555Driver_readPort(xl9555_port_t port, uint8_t *value);

/**
 * 设置单个输出引脚电平（仅对输出引脚有效）。
 * @param port   端口
 * @param mask   引脚掩码（如 XL9555_SPK_EN）
 * @param level  true = 高电平，false = 低电平
 */
esp_err_t xl9555Driver_setPin(xl9555_port_t port, uint8_t mask, bool level);

/**
 * 读取单个输入引脚电平。
 * @param port   端口
 * @param mask   引脚掩码（如 XL9555_KEY0）
 * @return true = 高电平，false = 低电平
 */
bool xl9555Driver_getPin(xl9555_port_t port, uint8_t mask);
