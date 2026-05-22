idf_component_register(
    SRCS "src/red_temp.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES uart_driver esp_common freertos
)
