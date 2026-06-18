idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_connect mic_sample red_temp mm_wave sleep_monitor rgb_led rgb_screen_16_16_4 beep cloud_mqtt
                        trans_2_cloud aw9523b rotary_encoder uart_driver classify_tflite wav_player)
