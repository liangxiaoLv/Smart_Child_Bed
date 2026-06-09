idf_component_register(
    SRCS "src/aw9523b_driver.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver driver esp_common esp_driver_gpio freertos
)
