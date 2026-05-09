#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

/**
 * 初始化一个 GPIO 为推挽输出，初始电平为 0。
 * @param pin  GPIO 编号
 */
esp_err_t gpioDriver_initOutput(gpio_num_t pin);

/**
 * 初始化一个 GPIO 为输入，可选上拉/下拉。
 * @param pin      GPIO 编号
 * @param pull_up  true = 内部上拉，false = 内部下拉
 */
esp_err_t gpioDriver_initInput(gpio_num_t pin, bool pull_up);

/**
 * 初始化一个 GPIO 为输入中断模式。
 * @param pin      GPIO 编号
 * @param intr_type  中断触发类型（GPIO_INTR_NEGEDGE 等）
 * @param handler    ISR 回调函数
 * @param arg        传给回调的参数
 */
esp_err_t gpioDriver_initInterrupt(gpio_num_t pin,
                                   gpio_int_type_t intr_type,
                                   gpio_isr_t handler,
                                   void *arg);

/** 设置输出电平，level: 0 或 1 */
esp_err_t gpioDriver_set(gpio_num_t pin, uint32_t level);

/** 读取输入电平，返回 0 或 1 */
int gpioDriver_get(gpio_num_t pin);
