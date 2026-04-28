#ifndef _KEY_H_
#define _KEY_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BOOT_GPIO_PIN GPIO_NUM_0 // Example GPIO pin for BOOT button, change as needed
#define BOOT gpio_get_level(BOOT_GPIO_PIN)
#define BOOT_PRES 1


void key_init(void);
uint8_t key_scan(uint8_t mode);

#endif