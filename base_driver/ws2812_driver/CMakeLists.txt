idf_component_register(
    SRCS "src/ws2812_driver.c" "src/led_strip_encoder.c"
    INCLUDE_DIRS "include" "src" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES driver esp_driver_rmt esp_driver_gpio
)
