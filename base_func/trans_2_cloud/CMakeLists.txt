idf_component_register(
    SRCS "src/trans_2_cloud.c"
    INCLUDE_DIRS "include"
    REQUIRES cloud_mqtt ens210 ens160 rgb_led wav_player
)
