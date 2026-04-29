#ifndef _ESPTIM_H_
#define _ESPTIM_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led.h"
#include "esp_timer.h"

void esptim_int_init(uint64_t tps);
void esptim_callback(void* arg);

#endif
