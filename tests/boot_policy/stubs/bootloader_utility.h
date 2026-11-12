#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define FACTORY_INDEX -1
#define ESP_PARTITION_TABLE_MAX_ENTRIES 96
#define ESP_PARTITION_TABLE_OFFSET 0x8000
#define ESP_PARTITION_MAGIC 0x50AA
#define PART_TYPE_DATA 1
typedef struct { uint32_t offset,size; } pos_t;
typedef struct { uint16_t magic; uint8_t type,subtype; pos_t pos; uint8_t label[16]; uint32_t flags; } esp_partition_info_t;
typedef struct { pos_t factory,ota[16]; } bootloader_state_t;
bool bootloader_utility_load_partition_table(bootloader_state_t *);
void bootloader_utility_load_boot_image(const bootloader_state_t *,int);
