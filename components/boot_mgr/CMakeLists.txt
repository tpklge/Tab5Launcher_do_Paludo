idf_component_register(
    SRCS "boot_mgr.c"
    INCLUDE_DIRS "include"
    REQUIRES storage
        board
        app_update
        bootloader_support
        esp_app_format
        esp_system
        log
        esp_timer
        freertos
)
