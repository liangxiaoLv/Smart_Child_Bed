#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_log.h"
// #include "key.h"
#include "ledc.h"
// #include "exit.h"
// #include "uart.h"
// #include "esptim.h"
#include "gptim.h"
#include "comm.h"

void app_main(void)
{
    U8 dir = 1;
    U16 ledpwmval = 0;

    ledc_config_t *ledc_config = malloc(sizeof(ledc_config_t));

    ledc_config->clk_cfg = LEDC_AUTO_CLK;
    ledc_config->timer_num = LEDC_PWM_TIMER;
    ledc_config->freq_hz = 1000;
    ledc_config->duty_resolution = LEDC_TIMER_14_BIT;  // 分辨率
    ledc_config->channel = LEDC_PWM_CH0_CHANNNEL;
    ledc_config->duty = 0;
    ledc_config->gpio_num = LEDC_PWM_CH0_GPIO;
    ledc_init(ledc_config);

    while(1) {
        vTaskDelay(50);
        if (dir == 1) {
            ledpwmval += 5;
        } else {
            ledpwmval -= 5;
        }

        if (ledpwmval == 100) {
            dir = 0;
        }
        if (ledpwmval == 0) {
            dir = 1;
        }
        ESP_LOGI("LED", "ledpwmval: %d", ledpwmval);
        ledc_pwm_set_duty(ledc_config, ledpwmval);
    }
}