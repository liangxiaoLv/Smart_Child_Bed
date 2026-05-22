idf_component_register(
    SRCS "src/beep.c"
    INCLUDE_DIRS "include"
    REQUIRES xl9555_driver i2c_driver esp_common freertos
)
