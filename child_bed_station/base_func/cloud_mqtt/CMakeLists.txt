idf_component_register(
    SRCS "src/mqtt_client.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__mqtt nvs_flash
)
