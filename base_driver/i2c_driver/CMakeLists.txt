idf_component_register(
    SRCS "src/i2c_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
)
