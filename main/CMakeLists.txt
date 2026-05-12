idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_connect red_temp mm_wave ens210 ens160 rgb_led beep)
