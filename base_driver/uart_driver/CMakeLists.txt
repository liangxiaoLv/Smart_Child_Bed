idf_component_register(
    SRCS "src/uart_driver.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES driver esp_driver_uart aw9523b
)
