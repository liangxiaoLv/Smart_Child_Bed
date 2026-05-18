idf_component_register(
    SRCS "src/wifi_connect.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_common
        xl9555_driver
        wav_player
        network_provisioning
        bt
        protocomm
        trans_2_cloud
)
