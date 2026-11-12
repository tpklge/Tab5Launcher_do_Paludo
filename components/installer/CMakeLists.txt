idf_component_register(
    SRCS "installer.c"
    INCLUDE_DIRS "include"
    REQUIRES wifi catalog partitions_mgr board
        firmware
        storage
        boot_mgr
        app_update
        bootloader_support
        spi_flash
        mbedtls
        esp_timer
        log
        freertos
        esp_system
)
