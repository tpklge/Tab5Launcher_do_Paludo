#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef struct { char name[64],icon[512]; } app_metadata_t;
bool app_registry_get(const uint8_t identity[32],app_metadata_t *out);
esp_err_t app_registry_set(const uint8_t identity[32],const app_metadata_t *metadata);
