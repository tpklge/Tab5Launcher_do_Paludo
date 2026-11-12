idf_component_register(SRCS "web.c" "web_security.c" "web_ops.c" INCLUDE_DIRS "include"
 REQUIRES mbedtls esp_http_server esp_timer esp_hw_support diagnostics installer boot_mgr storage board wifi json partitions_mgr usb_disk usb_storage pi_link lvgl)

target_compile_options(${COMPONENT_LIB} PRIVATE -fstack-usage -Wframe-larger-than=4096)
