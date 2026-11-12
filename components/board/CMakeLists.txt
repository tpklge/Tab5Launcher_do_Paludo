idf_component_register(
    SRCS
        "board.c" "board_sd.c" "board_rtc.c" "rtc_codec.c"
        "board_power.c"
        "power_model.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_lvgl_port
        m5stack_tab5
        esp_driver_spi
        esp_driver_sdspi
        driver
        fatfs
        storage
        json
        sdmmc
        log
        esp_system
        esp_timer
        freertos
)
