idf_component_register(
    SRCS "src/sleep_monitor.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES uart_driver esp_common freertos cloud_mqtt
)
