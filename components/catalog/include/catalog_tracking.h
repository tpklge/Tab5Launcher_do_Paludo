#pragma once
#include "catalog.h"
#define CATALOG_UPDATE_MAX 16
typedef struct {int slot;char previous[32],source[512];catalog_item_t item;bool changed;} catalog_update_t;
typedef struct {unsigned count,untracked,failed;catalog_update_t rows[CATALOG_UPDATE_MAX];} catalog_updates_t;
esp_err_t catalog_download_from(const catalog_item_t*,const char*source,char*,size_t,catalog_progress_fn,void*);
esp_err_t catalog_record_download(const char*path,const catalog_item_t*,const char*source);
esp_err_t catalog_track_install(const char*path,const uint8_t identity[32]);
esp_err_t catalog_link_slot(int,const catalog_item_t*,const char*source);
esp_err_t catalog_check_updates(catalog_updates_t*,catalog_progress_fn,void*);
esp_err_t catalog_download_updates(const catalog_updates_t*,catalog_progress_fn,void*,char*,size_t);

esp_err_t catalog_alias_download(const char *original,const char *staged);
