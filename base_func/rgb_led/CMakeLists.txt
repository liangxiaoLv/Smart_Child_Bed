idf_component_register(
    SRCS "src/rgb_led.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES ws2812_driver driver esp_timer esp_common freertos
)
