#pragma once
/* diagnostics — reset reason, heap stats, circular log, crash records. */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

void diagnostics_early_init(void);

typedef struct {
    int   reset_reason;    /* esp_reset_reason_t */
    char  reset_desc[32];
    uint32_t free_internal;
    uint32_t free_psram;
    uint32_t min_free_internal;
    char  idf_ver[32];
    char  app_ver[32];
} diagnostics_snapshot_t;

void diagnostics_get_snapshot(diagnostics_snapshot_t *out);

/* Circular log: append a line (truncated to 120 chars). */
void diagnostics_log(const char *line);

/* Export log to file. */
esp_err_t diagnostics_export_log(const char *path);

#ifdef __cplusplus
}
#endif

/* Worker-only BMP screenshot, exclusive destination creation, bounded 1280x720. */
esp_err_t diagnostics_capture(const char *path);
