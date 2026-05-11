idf_component_register(
    SRCS "src/uart_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_uart
)
