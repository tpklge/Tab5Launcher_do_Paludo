idf_component_register(
    SRCS "time_mgr.c" "date_pt.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        storage
        esp_netif
        log
        esp_system
        freertos
        lwip
)
