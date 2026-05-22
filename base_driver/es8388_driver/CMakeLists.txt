idf_component_register(
    SRCS "src/es8388_driver.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver freertos
)
