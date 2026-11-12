idf_component_register(SRCS "usb.c" "c6_update.c" INCLUDE_DIRS "include"
 REQUIRES web catalog console diagnostics boot_mgr backup storage installer wifi board json mbedtls usb_disk usb_storage esp_hosted firmware)
