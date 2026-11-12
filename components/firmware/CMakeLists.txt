idf_component_register(
    SRCS "firmware.c" "firmware_parser.c"
    INCLUDE_DIRS "include"
    REQUIRES storage json
        mbedtls
        log
        freertos
        esp_system
)
