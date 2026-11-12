include("${CMAKE_CURRENT_LIST_DIR}/../cmake/dependency_fixes.cmake")

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        energy
        pi_link
        web
        usb_mgr
        usb_storage
        partitions_mgr
        installer
        board
        boot_mgr
        storage
        time_mgr
        input
        ui
        wifi
        diagnostics
        esp_system
        log
        freertos
)
