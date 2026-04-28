#ifndef LED_H
#define LED_H

#include <driver/gpio.h>

#define LED_GPIO_PIN GPIO_NUM_1 // Example GPIO pin for LED, change as needeD

typedef enum {
    PIN_RESET = 0,
    PIN_SET
} GPIO_OUTPUT_STATE;

#define LED(X) do { X? \
    gpio_set_level(LED_GPIO_PIN, PIN_SET) : \
    gpio_set_level(LED_GPIO_PIN, PIN_RESET); } while(0)

#define LED_TOGGLE() do { \
    int current_level = gpio_get_level(LED_GPIO_PIN); \
    gpio_set_level(LED_GPIO_PIN, !current_level); \
} while(0)

void led_init(void);

#endif // LED_H