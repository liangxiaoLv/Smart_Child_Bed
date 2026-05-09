idf_component_register(
    SRCS "src/xl9555_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES i2c_driver driver esp_common
)
