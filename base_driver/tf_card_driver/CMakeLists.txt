idf_component_register(
    SRCS "src/tf_card_driver.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES esp_driver_spi esp_driver_sdspi sdmmc fatfs
)
