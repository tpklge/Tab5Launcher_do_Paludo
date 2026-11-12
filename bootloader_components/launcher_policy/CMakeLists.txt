idf_component_register(SRCS "hooks.c" INCLUDE_DIRS "../../components/boot_mgr/include" REQUIRES bootloader_support spi_flash)
idf_component_get_property(boot_support_dir bootloader_support COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${boot_support_dir}/private_include")
