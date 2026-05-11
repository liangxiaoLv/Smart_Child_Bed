idf_component_register(
    SRCS "src/mm_wave.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES uart_driver esp_common freertos
)
