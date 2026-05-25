idf_component_register(
    SRCS "src/rotary_encoder.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES driver esp_driver_gpio esp_common freertos esp_timer
)
