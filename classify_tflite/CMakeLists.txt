idf_component_register(
    SRCS
        "esp_audio_classifier.c"
        "classify_tflite_service.c"
        "tflm_bridge.cc"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp-tflite-micro cloud_mqtt esp_http_client
)

target_add_binary_data(${COMPONENT_LIB} "best_model.tflite" BINARY)
