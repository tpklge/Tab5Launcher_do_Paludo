idf_component_register(
    SRCS "storage.c" "launcher_paths.c" "launcher_store.c" "config_json.c" "app_registry.c" "app_data.c" "launcher_state.c" "sd_files.c" "storage_guard.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        mbedtls
        esp_timer
        json
        fatfs
        log
        esp_system
        freertos
)
