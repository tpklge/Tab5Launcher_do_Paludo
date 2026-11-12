#include "storage_guard.h"
/* diagnostics.c */
#include "diagnostics.h"
#include "esp_app_desc.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "diagnostics";

#define LOG_LINES 64
#define LOG_LINE_LEN 128

static EXT_RAM_BSS_ATTR char s_log[LOG_LINES][LOG_LINE_LEN];
static int s_log_head = 0;
static int s_log_count = 0;
static SemaphoreHandle_t s_lock = NULL;

static vprintf_like_t s_previous;
static int capture(const char *format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  char line[LOG_LINE_LEN];
  vsnprintf(line, sizeof(line), format, copy);
  va_end(copy);
  diagnostics_log(line);
  return s_previous ? s_previous(format, args) : 0;
}

void diagnostics_early_init(void) {
  s_lock = xSemaphoreCreateMutex();
  if (s_lock)
    s_previous = esp_log_set_vprintf(capture);
  ESP_LOGI(TAG, "reset reason: %d, idf: %s", (int)esp_reset_reason(),
           esp_get_idf_version());
}

static const char *reset_reason_str(int r) {
  switch (r) {
  case ESP_RST_POWERON:
    return "POWER_ON";
  case ESP_RST_EXT:
    return "RESET_EXT";
  case ESP_RST_SW:
    return "SW_RESET";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WATCHDOG";
  case ESP_RST_TASK_WDT:
    return "TASK_WATCHDOG";
  case ESP_RST_WDT:
    return "OTHER_WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEP_SLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  default:
    return "UNKNOWN";
  }
}

void diagnostics_get_snapshot(diagnostics_snapshot_t *out) {
  memset(out, 0, sizeof(*out));
  out->reset_reason = (int)esp_reset_reason();
  snprintf(out->reset_desc, sizeof(out->reset_desc), "%s",
           reset_reason_str(out->reset_reason));
  out->free_internal = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  out->free_psram = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  out->min_free_internal =
      (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  snprintf(out->idf_ver, sizeof(out->idf_ver), "%s", esp_get_idf_version());
  const esp_app_desc_t *desc = esp_app_get_description();
  if (desc)
    snprintf(out->app_ver, sizeof(out->app_ver), "%s", desc->version);
}

void diagnostics_log(const char *line) {
  if (!s_lock || !line || xSemaphoreTake(s_lock, 0) != pdTRUE)
    return;
  snprintf(s_log[s_log_head], LOG_LINE_LEN, "%s", line);
  s_log_head = (s_log_head + 1) % LOG_LINES;
  if (s_log_count < LOG_LINES)
    s_log_count++;
  xSemaphoreGive(s_lock);
}

esp_err_t diagnostics_export_log(const char *path) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!path || !s_lock)
    return ESP_ERR_INVALID_ARG;
  char *copy = calloc(LOG_LINES, LOG_LINE_LEN + 1);
  if (!copy)
    return ESP_ERR_NO_MEM;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t used = 0;
  int start = s_log_count < LOG_LINES ? 0 : s_log_head;
  for (int i = 0; i < s_log_count; i++)
    used += snprintf(copy + used, LOG_LINES * (LOG_LINE_LEN + 1) - used, "%s\n",
                     s_log[(start + i) % LOG_LINES]);
  xSemaphoreGive(s_lock);
  FILE *f = fopen(path, "wx");
  if (!f) {
    free(copy);
    return ESP_ERR_NOT_FOUND;
  }
  diagnostics_snapshot_t d;
  diagnostics_get_snapshot(&d);
  bool failed = fprintf(f,
                        "Tab5 %s / IDF %s / reset %s / internal %lu / minimum "
                        "%lu / PSRAM %lu\n",
                        d.app_ver, d.idf_ver, d.reset_desc,
                        (unsigned long)d.free_internal,
                        (unsigned long)d.min_free_internal,
                        (unsigned long)d.free_psram) < 0;
  if (fwrite(copy, 1, used, f) != used)
    failed = true;
  if (fclose(f))
    failed = true;
  free(copy);
  return failed ? ESP_FAIL : ESP_OK;
}
