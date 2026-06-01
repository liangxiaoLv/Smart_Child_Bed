idf_component_register(
    SRCS "src/ap3216c.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver esp_common freertos
)
