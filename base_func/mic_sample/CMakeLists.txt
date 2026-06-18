idf_component_register(
    SRCS "src/mic_sample.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES es7210_driver cloud_mqtt freertos esp_timer esp_driver_gpio classify_tflite
)
