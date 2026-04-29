#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include "esp_log.h"
// #include "key.h"
#include "led.h"
// #include "exit.h"
// #include "uart.h"
// #include "esptim.h"
#include "gptim.h"

void app_main(void)
{
    led_init();
    // uart_init(115200);
    // exit_init();
    timg_config_t *timcfg = malloc(sizeof(timg_config_t));
    timcfg->timing_time = 1000000;   // 1s
    timcfg->timer_idx = 0;
    timcfg->timer_group = 0;
    timcfg->auto_reload = false;
    timcfg->alarm_value = timcfg->timing_time;
    timcfg->timer_counter_value = 0;
    timg_init(timcfg);
    while(1) {
        if(timcfg->timer_counter_value !=0) {
            ESP_LOGI("main Timer", "Timer autoreload, count value in isr: %llu", timcfg->timer_counter_value);
            timcfg->timer_counter_value = 0;
        }
        vTaskDelay(10);
    }

}