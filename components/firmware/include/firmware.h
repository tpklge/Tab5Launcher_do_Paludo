#pragma once
#include "esp_err.h"
#include "firmware_parser.h"
#define FW_SOURCE_PATH_MAX 512
#define FW_DATA_MAX 3
typedef struct {
  char path[FW_SOURCE_PATH_MAX], label[17];
  uint32_t offset, length;
  uint8_t sha256[32];
} firmware_region_t;
typedef enum {
  FW_APP_ONLY,
  FW_MERGED,
  FW_PACKAGE,
  FW_RAW_BACKUP,
  FW_DATA_BACKUP
} firmware_kind_t;
typedef struct {
  firmware_image_t image;
  uint8_t sha256[32];
  firmware_kind_t kind;
  firmware_region_t app, data[FW_DATA_MAX];
  unsigned data_count;
  uint8_t source_sha256[32];
} firmware_info_t;
esp_err_t firmware_init(void);
esp_err_t firmware_analyze_file(const char *path, firmware_info_t *info,
                                char *error, size_t size);
esp_err_t firmware_hash_region(const firmware_region_t *region,
                               uint8_t digest[32]);
