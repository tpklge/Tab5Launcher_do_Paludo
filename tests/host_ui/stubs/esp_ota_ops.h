#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_PARTITION_TYPE_APP 0
#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
typedef struct { uint32_t address,size; char label[17]; } esp_partition_t;
typedef struct { char project_name[32],version[32]; } esp_app_desc_t;
const esp_partition_t *esp_partition_find_first(int,int,const char*);
esp_err_t esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
esp_err_t esp_ota_get_partition_description(const esp_partition_t*,esp_app_desc_t*);

esp_err_t esp_partition_get_sha256(const esp_partition_t*,uint8_t[32]);

#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_ANY 255
