idf_component_register(
    SRCS "src/sensor_rgb.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver ap3216c rgb_led esp_common freertos
)
