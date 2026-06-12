idf_component_register(
    SRCS "src/mic_adc.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES esp_codec_dev esp_driver_i2s
)
