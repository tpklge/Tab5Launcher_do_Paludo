idf_component_register(
    SRCS "wifi_mgr.c" "wifi_store.c" "wifi_link.c"
    INCLUDE_DIRS "include"
    REQUIRES
        storage
        board
        esp_wifi
        esp_wifi_remote
        esp_hosted
        esp_netif
        log
        freertos
        esp_event
        esp_system
        esp_timer
)
