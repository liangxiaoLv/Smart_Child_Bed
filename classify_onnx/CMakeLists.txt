idf_component_register(
    SRCS
        "classify_onnx_service.c"
        "onnx_bridge.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES classify_tflite cloud_mqtt esp_http_client
)

target_add_binary_data(${COMPONENT_LIB} "best_model.onnx" BINARY)
