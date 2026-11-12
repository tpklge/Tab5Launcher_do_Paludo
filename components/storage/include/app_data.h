#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define APP_DATA_MAX 8
typedef bool (*app_data_progress_fn)(unsigned,const char*,void*);
typedef struct {char label[17];uint32_t address,size;bool nvs;} app_data_region_t;
typedef struct {uint8_t identity[32];unsigned count;app_data_region_t regions[APP_DATA_MAX];} app_data_config_t;
bool app_data_get(const uint8_t[32],app_data_config_t*);
esp_err_t app_data_configure(const app_data_config_t*,bool capture_current,app_data_progress_fn,void*,char*,size_t);
esp_err_t app_data_activate(int slot,app_data_progress_fn,void*,char*,size_t);
esp_err_t app_data_capture_return(app_data_progress_fn,void*,char*,size_t);
esp_err_t app_data_save_active(app_data_progress_fn,void*,char*,size_t);
bool app_data_recovery_required(void);

esp_err_t app_data_recover(bool rollback,app_data_progress_fn,void*,char*,size_t);
esp_err_t app_data_inherit(const uint8_t old_id[32],const uint8_t new_id[32],app_data_progress_fn,void*,char*,size_t);

esp_err_t app_data_release_owner(void);
