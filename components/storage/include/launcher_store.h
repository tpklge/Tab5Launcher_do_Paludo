#pragma once
#include "esp_err.h"
#include <stddef.h>
#ifndef LAUNCHER_ROOT
#define LAUNCHER_ROOT "/sdcard/launcher"
#endif
#define LAUNCHER_PRIVATE LAUNCHER_ROOT "/private"
#define LAUNCHER_WEB LAUNCHER_ROOT "/web"
/* SD is authoritative. No writes to flash/NVS. Bounded, CRC-checked records. */
esp_err_t launcher_store_init(void);
esp_err_t launcher_store_read(const char *key,void *data,size_t *size);
esp_err_t launcher_store_write(const char *key,const void *data,size_t size);
/* Atomic raw file publication within /launcher (HTML/config without a header). */
esp_err_t launcher_file_write(const char *path,const void *data,size_t size);
/* Copy a verified uploaded web asset into /launcher/web; fixed filenames only. */
esp_err_t launcher_web_publish(const char *source,const char *name);
#define LAUNCHER_BACKUPS LAUNCHER_ROOT "/backups"
#define LAUNCHER_DOWNLOADS LAUNCHER_ROOT "/downloads"
#define LAUNCHER_INCOMING LAUNCHER_ROOT "/incoming"
#define LAUNCHER_DIAGNOSTICS LAUNCHER_ROOT "/diagnostics"
esp_err_t launcher_paths_migrate(void);
esp_err_t launcher_adopt_image(const char *source,const char *folder,char *dest,size_t size);

/* Validators distinguish valid candidates from corrupt/interrupted publication.
 * Recovery preserves displaced copies under unique .recovered-N names. */
typedef esp_err_t (*launcher_file_validator_fn)(const char *, void *);
esp_err_t launcher_file_recover(const char *, launcher_file_validator_fn, void *);
esp_err_t launcher_file_write_validated(const char *, const void *, size_t,
                                       launcher_file_validator_fn, void *);
