#pragma once
#include <stdint.h>
int esp_flash_get_size(void *chip,uint32_t *size);
#include <stddef.h>
int esp_flash_read(void*,void*,uint32_t,uint32_t);
int esp_flash_write(void*,const void*,uint32_t,uint32_t);
int esp_flash_erase_region(void*,uint32_t,uint32_t);
