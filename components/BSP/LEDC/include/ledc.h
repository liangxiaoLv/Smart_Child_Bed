#ifndef _LEDC_H
#define _LEDC_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "comm.h"

#define LEDC_PWM_TIMER              LEDC_TIMER_0
#define LEDC_PWM_CH0_GPIO           GPIO_NUM_1
#define LEDC_PWM_CH0_CHANNNEL       LEDC_CHANNEL_0

typedef struct
{
    ledc_clk_cfg_t clk_cfg;
    ledc_timer_t timer_num;
    U32 freq_hz;
    ledc_timer_bit_t duty_resolution;
    ledc_channel_t channel;
    U32 duty;
    int gpio_num;
} ledc_config_t;

void ledc_init(ledc_config_t *ledc_config);
void ledc_pwm_set_duty(ledc_config_t *ledc_config, U32 duty);

#endif // LEDC_H    