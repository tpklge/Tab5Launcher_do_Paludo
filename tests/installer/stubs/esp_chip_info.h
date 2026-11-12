#pragma once
#include <stdint.h>
typedef struct { uint16_t revision; } esp_chip_info_t;
static inline void esp_chip_info(esp_chip_info_t *info) { info->revision=103; }
