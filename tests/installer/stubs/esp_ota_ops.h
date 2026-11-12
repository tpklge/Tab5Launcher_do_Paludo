#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
typedef struct { uint32_t address,size; char label[17]; unsigned type,subtype; bool encrypted,readonly; } esp_partition_t;
typedef struct { char project_name[32],version[32]; } esp_app_desc_t;
typedef unsigned esp_ota_handle_t;
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_partition_find_first(int,int,const char*);
int esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);
int esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
int esp_partition_get_sha256(const esp_partition_t*,uint8_t digest[32]);
int esp_ota_begin(const esp_partition_t*,size_t,esp_ota_handle_t*);
int esp_ota_write(esp_ota_handle_t,const void*,size_t);
int esp_ota_end(esp_ota_handle_t);
int esp_ota_abort(esp_ota_handle_t);

int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);

int esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);

typedef unsigned esp_partition_subtype_t;
#define ESP_PARTITION_SUBTYPE_APP_FACTORY 0
int esp_ota_set_boot_partition(const esp_partition_t*);

#define ESP_PARTITION_SUBTYPE_DATA_PHY 1
#define ESP_PARTITION_SUBTYPE_DATA_OTA 0
#define ESP_PARTITION_SUBTYPE_DATA_COREDUMP 3
typedef void *esp_partition_iterator_t;
esp_partition_iterator_t esp_partition_find(int,int,const char*);
const esp_partition_t *esp_partition_get(esp_partition_iterator_t);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t);
#define ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS 4
