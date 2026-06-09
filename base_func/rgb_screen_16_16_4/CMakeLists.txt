idf_component_register(
    SRCS "src/rgb_screen_16_16_4.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES ws2812_driver freertos esp_common
)
