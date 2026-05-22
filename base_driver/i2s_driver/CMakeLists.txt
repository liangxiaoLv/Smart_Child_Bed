idf_component_register(
    SRCS "src/i2s_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2s freertos
)
