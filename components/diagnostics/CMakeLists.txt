idf_component_register(
    SRCS "diagnostics.c" "screenshot.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board
        storage
        lvgl
        esp_system
        esp_app_format
        log
        heap
        freertos
)
