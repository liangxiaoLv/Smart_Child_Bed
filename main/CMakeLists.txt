idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_connect red_temp mm_wave ens210 ens160 rgb_led rgb_screen_8_32 beep cloud_mqtt trans_2_cloud wav_player xl9555_driver)
