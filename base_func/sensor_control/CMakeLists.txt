idf_component_register(
    SRCS "src/sensor_control.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES ap3216c_driver rgb_led i2c_driver esp_common freertos
)
