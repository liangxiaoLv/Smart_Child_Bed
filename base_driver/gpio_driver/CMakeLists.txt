idf_component_register(
    SRCS "src/gpio_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common esp_driver_gpio
)
