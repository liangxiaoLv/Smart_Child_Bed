#include "ledc.h"

U32 ledc_duty_pow(U32 duty, U8 m, U8 n)
{
    U32 result = 1;
    while (n--) {
        result *= m;
    }
    return (result * duty) / 100;
}

void ledc_init(ledc_config_t *ledc_config)
{
    ledc_config->duty = ledc_duty_pow(ledc_config->duty, 2, ledc_config->duty_resolution);
    ledc_timer_config_t timer_conf = {
        .duty_resolution = ledc_config->duty_resolution,
        .freq_hz = ledc_config->freq_hz,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = ledc_config->timer_num,
        .clk_cfg = ledc_config->clk_cfg,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ledc_channel = {
        .channel = ledc_config->channel,
        .duty = ledc_config->duty,
        .gpio_num = ledc_config->gpio_num,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .intr_type = LEDC_INTR_DISABLE,
        .hpoint = 0,
        .timer_sel = ledc_config->timer_num,
    };
    ledc_channel_config(&ledc_channel);
}

void ledc_pwm_set_duty(ledc_config_t *ledc_config, U32 duty)
{
    ledc_config->duty = ledc_duty_pow(duty, 2, ledc_config->duty_resolution);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_config->channel, ledc_config->duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_config->channel);
}