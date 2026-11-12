#pragma once
#include <stdbool.h>
extern void *esp_flash_default_chip;
int esp_flash_set_dangerous_write_protection(void*,bool);
