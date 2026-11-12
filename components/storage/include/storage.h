#pragma once
/* Launcher configuration and credentials are authoritative on the SD card.
 * /launcher holds every persistent launcher preference and operational journal.
 * No launcher NVS partition or namespace is initialized or imported. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "launcher_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_CFG_VERSION   1
#define STORAGE_SD_MOUNT      "/sdcard"
#define STORAGE_CFG_PATH      LAUNCHER_ROOT "/config.json"

typedef struct {
    uint8_t  bright;           /* backlight %, 5–100 */
    uint16_t dim_s;            /* idle s before dim, 0=off */
    uint16_t screen_off_s;     /* idle s before backlight off, 0=never */
    char     color[12];        /* accent id: "graphite", "app", "wall", hex */
    char     theme[12];        /* Theme name from theme_catalog.h. */
    uint16_t rot;              /* 0 or 180 */
    bool     ask_spiffs;
    bool     only_bins;
    uint8_t  boot_time_s;      /* countdown, 1–10 */
    bool     wifi_autoconnect;
    bool     deep_sleep_start;
    bool     btn_only_start;
    char     timezone[32];     /* POSIX TZ string, e.g. "<-04>4" for America/Cuiaba */
    char     timezone_id[32];  /* human id, e.g. "America/Cuiaba" */
    bool     idle_clock;
    uint8_t  clock_style;      /* 0=digital 1=big 2=thin 3=analog */
    uint8_t  clock_back;       /* 0=blur 1=clear 2=black */
    uint8_t  wall_dim;         /* 0=light 1=medium 2=dark */
    char     wallpaper[96];    /* "" = wallpaper.jpg, "-" = none */
    char     scheme[12];       /* partition scheme id */
    bool power_logging,battery_warning,critical_shutdown;
    bool     pi_enabled;
    bool     pi_auto_backup;
} launcher_config_t;

/* Defaults, validated SD seed, then authoritative NVS preferences. */
void storage_init(void);

/* Access current config (read-only pointer; stable until storage_save). */
const launcher_config_t *storage_get_config(void);
/* Thread-safe copy for workers and network services. */
void storage_snapshot(launcher_config_t *out);

/* Mutable access: modify fields, then call storage_save. */
launcher_config_t *storage_get_config_rw(void);

/* Persist current config to both NVS and config.conf on SD (if mounted). */
esp_err_t storage_save(void);

/* Reset appearance only (theme, color, wallpaper). */
esp_err_t storage_reset_appearance(void);

/* Full factory reset (all config to defaults). Does NOT erase NVS namespace for Wi-Fi. */
esp_err_t storage_reset_all(void);

/* Export config to JSON (excludes credentials). dest must be a valid writable path. */
esp_err_t storage_export_json(const char *dest_path);

/* Import config from JSON file (does not overwrite credentials or partition info). */
esp_err_t storage_import_json(const char *src_path);

#ifdef __cplusplus
}
#endif

/* Validated snapshot update; call from a worker, not an LVGL event. */
esp_err_t storage_update(const launcher_config_t *config);

esp_err_t storage_import_theme(const char *path);
