idf_component_register(
    SRCS
        "ui.c" "ui_picker.c" "fonts/ui_latin20.c" "ui_visual.c" "ui_assets.c" "ui_form.c"
        "ui_nav.c"
        "ui_progress.c"
        "ui_home.c"
        "ui_boot.c"
        "ui_status_bar.c"
        "ui_theme.c"
        "ui_wifi.c"
        "ui_sd.c"
        "ui_cfg.c"
        "ui_diag.c" "ui_services.c"
    INCLUDE_DIRS "include"
    REQUIRES
        partitions_mgr
        installer
        firmware
        board
        boot_mgr
        storage
        usb_storage
        time_mgr
        input
        wifi
        diagnostics
        pi_link
        backup
        catalog
        web
        esp_lvgl_port
        lvgl
        log
        freertos
        esp_system
        vfs
        esp_app_format
        app_update
)


# UI callback frames must fit taskLVGL; workers have explicitly larger stacks.
target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Wframe-larger-than=4096)
option(TAB5_UI_NAV_SELFTEST "Read-only HOME/SD navigation smoke test on device" OFF)
if(TAB5_UI_NAV_SELFTEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TAB5_UI_NAV_SELFTEST=1)
endif()
