idf_component_register(
    SRCS "src/es7210_drv.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver esp_driver_i2s
)
