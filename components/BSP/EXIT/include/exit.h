#ifndef _EXIT_H_
#define _EXIT_H_

#include "driver/gpio.h"
#include "esp_system.h"
#include "led.h"

#define BOOT_INT_GPIO_PIN GPIO_NUM_0 // Example GPIO pin for EXIT button, change as needed
#define BOOT gpio_get_level(BOOT_INT_GPIO_PIN)
void exit_init(void) ;
#endif
