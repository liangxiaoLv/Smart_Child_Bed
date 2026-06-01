idf_component_register(
    SRCS "src/tf_card.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    REQUIRES tf_card_driver
)
