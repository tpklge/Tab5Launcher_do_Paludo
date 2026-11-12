#pragma once
#include "bootloader_utility.h"
int bootloader_flash_read(size_t,void *,size_t,bool);
int bootloader_flash_write(size_t,void *,size_t,bool);
int bootloader_flash_erase_range(uint32_t,uint32_t);
