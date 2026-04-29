#include "esptim.h"

void esptim_int_init(uint64_t tps)
{
    esp_timer_handle_t esp_tim_handle;

    esp_timer_create_args_t tim_periodic_args = {
        .callback = esptim_callback,
        .arg = NULL,
    };
    esp_timer_create(&tim_periodic_args, &esp_tim_handle);
    esp_timer_start_periodic(esp_tim_handle, tps);
}

void esptim_callback(void* arg)
{
    LED_TOGGLE();
}