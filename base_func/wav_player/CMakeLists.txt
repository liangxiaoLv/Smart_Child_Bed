idf_component_register(
    SRCS "src/wav_player.c"
    INCLUDE_DIRS "include" "${CMAKE_SOURCE_DIR}/firm_info"
    EMBED_FILES "src/start_connect.wav" "src/wifi_connect_ok.wav"
    REQUIRES i2s_driver aw883xx_driver esp_common freertos
)
