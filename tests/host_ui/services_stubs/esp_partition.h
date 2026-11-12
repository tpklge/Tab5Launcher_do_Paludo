#pragma once
#include <stdint.h>
typedef struct {unsigned type,subtype;uint32_t address,size;char label[17];} esp_partition_t;
typedef void *esp_partition_iterator_t;
#define ESP_PARTITION_TYPE_ANY 255
#define ESP_PARTITION_SUBTYPE_ANY 255
#define ESP_PARTITION_TYPE_APP 0
static esp_partition_t partition={.type=0,.subtype=16,.address=0x210000,.size=0x400000,.label="app0"};
static inline void *esp_partition_find(int a,int b,const char*c){return &partition;}
static inline const esp_partition_t *esp_partition_get(void *p){return p;}
static inline void *esp_partition_next(void *p){return 0;}

#define ESP_PARTITION_SUBTYPE_APP_OTA_0 16
static inline const esp_partition_t*esp_partition_find_first(int a,int b,const char*c){return b==16?&partition:0;}
