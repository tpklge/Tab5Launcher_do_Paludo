idf_component_register(SRCS "partitions_mgr.c" "layout_model.c" "flash_snapshot.c" INCLUDE_DIRS "include"
 REQUIRES boot_mgr storage spi_flash app_update mbedtls bootloader_support freertos)
