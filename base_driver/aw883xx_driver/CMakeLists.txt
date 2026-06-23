idf_component_register(
    SRCS "src/aw883xx_driver.c"
         "src/aw883xx_device.c"
         "src/aw883xx_pid_2183_init.c"
    INCLUDE_DIRS "include"
                 "src"
                 "config/aw88399_i2s_mono_16"
                 "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES i2c_driver driver esp_driver_gpio esp_common freertos
)
