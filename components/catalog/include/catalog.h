#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CATALOG_MAX_ITEMS 128
#define CATALOG_MAX_PARTS 5
typedef struct { char file[96],url[512],sha256[65]; uint32_t size; } catalog_part_t;
typedef struct { char id[48],name[96],version[32],description[1024],cover_url[512],cover_path[160]; unsigned count; catalog_part_t parts[CATALOG_MAX_PARTS]; } catalog_item_t;
typedef struct { unsigned count; catalog_item_t items[CATALOG_MAX_ITEMS]; } catalog_list_t;
typedef bool (*catalog_progress_fn)(unsigned,const char *,void *);
esp_err_t catalog_init(void);
bool catalog_https_url(const char *url);
/* Strict schema 1, bounded 32 entries x 5 files, no executable expressions. */
esp_err_t catalog_parse(const char *json,catalog_list_t *out);
/* Blocking worker-only. HTTPS with bounded redirects and SD cache. */
esp_err_t catalog_fetch(const char *url,catalog_list_t *out,char *error,size_t size);
/* Downloads resumable parts into a manifest-specific directory; verified publication.
 * entry returns the local entrypoint, later analyzed/confirmed in SD. */
esp_err_t catalog_download(const catalog_item_t *item,char *entry,size_t size,catalog_progress_fn progress,void *user);
esp_err_t catalog_source_set(const char *url);
void catalog_source_get(char *url,size_t size);

/* Also accepts Tab5/P4 LauncherHub entries and {version,url,notes} releases. */
esp_err_t catalog_parse_any(const char *json,const char *source,catalog_list_t *out);

esp_err_t catalog_fetch_progress(const char *url,catalog_list_t *out,char *error,size_t size,catalog_progress_fn progress,void *user);

/* Filtered model on SD: quick offline/fresh reuse; force bypasses cache. */
esp_err_t catalog_fetch_refresh(const char*,catalog_list_t*,char*,size_t,catalog_progress_fn,void*);
esp_err_t catalog_cache_read(const char*,catalog_list_t*,int64_t *age);
esp_err_t catalog_cache_write(const char*,const catalog_list_t*);
esp_err_t catalog_probe(catalog_item_t*,catalog_progress_fn,void*);

/* Update checks fail closed on network failure; never report cached data as fresh. */
esp_err_t catalog_fetch_latest(const char*,catalog_list_t*,char*,size_t,catalog_progress_fn,void*);
/* Manual /launcher/ota_sources.json; no source selector in the UI. */
esp_err_t catalog_fetch_sources(catalog_list_t*,char (*origins)[512],bool refresh,char*,size_t,catalog_progress_fn,void*);
