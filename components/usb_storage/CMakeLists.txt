idf_component_register(SRCS "usb_storage.c" INCLUDE_DIRS "include" REQUIRES usb_host_bus usb_host_msc storage fatfs freertos esp_timer)
