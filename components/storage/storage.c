#include "storage_guard.h"
/* storage.c — configuration on SD. */
#include "cJSON.h"
#include "config_json.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "json_limits.h"
#include "storage.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "storage";

static launcher_config_t s_cfg;
static SemaphoreHandle_t s_lock = NULL;

/* Default values */
static void config_defaults(launcher_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->bright = 80;
  c->dim_s = 60;
  c->screen_off_s = 300;
  strncpy(c->color, "graphite", sizeof(c->color) - 1);
  strncpy(c->theme, "graphite", sizeof(c->theme) - 1);
  c->rot = 0;
  c->ask_spiffs = true;
  c->only_bins = true;
  c->boot_time_s = 3;
  c->wifi_autoconnect = false;
  c->deep_sleep_start = false;
  c->btn_only_start = false;
  /* America/Cuiaba = UTC-4, no DST */
  strncpy(c->timezone, "<-04>4", sizeof(c->timezone) - 1);
  strncpy(c->timezone_id, "America/Cuiaba", sizeof(c->timezone_id) - 1);
  c->idle_clock = true;
  c->clock_style = 0;
  c->clock_back = 0;
  c->wall_dim = 1;
  c->power_logging = true;
  c->battery_warning = true;
  c->critical_shutdown = false;
  strncpy(c->wallpaper, "", sizeof(c->wallpaper) - 1);
  strncpy(c->scheme, "default", sizeof(c->scheme) - 1);
  c->pi_enabled = false;
  c->pi_auto_backup = false;
}

static esp_err_t sd_config_load(launcher_config_t *c, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_ERR_NOT_FOUND;
  char *text = calloc(1, 8193);
  if (!text) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t n = fread(text, 1, 8192, f);
  bool bad = ferror(f) || !feof(f) || memchr(text, 0, n);
  fclose(f);
  esp_err_t err = bad ? ESP_ERR_INVALID_ARG : config_json_decode(text, c);
  free(text);
  return err;
}

static esp_err_t config_valid_file(const char *path, void *unused) {
  launcher_config_t candidate;
  config_defaults(&candidate);
  return sd_config_load(&candidate, path);
}
static esp_err_t config_write(const char *path, const launcher_config_t *c) {
  if (!path || strlen(path) > 500)
    return ESP_ERR_INVALID_ARG;
  char *text = config_json_encode(c);
  if (!text)
    return ESP_ERR_NO_MEM;
  esp_err_t e = launcher_file_write_validated(path, text, strlen(text),
                                              config_valid_file, NULL);
  free(text);
  return e;
}

/* ---- Public API ---------------------------------------------------------- */

void storage_init(void) {
  s_lock = xSemaphoreCreateMutex();
  config_defaults(&s_cfg);
  launcher_store_init();
  esp_err_t recovery =
      launcher_file_recover(STORAGE_CFG_PATH, config_valid_file, NULL);
  if (recovery != ESP_OK && recovery != ESP_ERR_NOT_FOUND)
    ESP_LOGW(TAG, "Config recovery incomplete; retained copies preserved");
  esp_err_t loaded = sd_config_load(&s_cfg, STORAGE_CFG_PATH);
  esp_err_t primary = loaded;
  if (loaded != ESP_OK)
    loaded = sd_config_load(&s_cfg, STORAGE_CFG_PATH ".previous");
  if (loaded == ESP_ERR_NOT_FOUND && primary == ESP_ERR_NOT_FOUND) {
    /* One-time import of legacy SD; an existing invalid config never
     * silently falls back to older internal preferences. */
    sd_config_load(&s_cfg, STORAGE_SD_MOUNT "/config.conf");
    if (!config_json_valid(&s_cfg))
      config_defaults(&s_cfg);
    if (config_write(STORAGE_CFG_PATH, &s_cfg) == ESP_OK &&
        access(LAUNCHER_ROOT "/config.legacy.conf", F_OK) != 0)
      rename(STORAGE_SD_MOUNT "/config.conf",
             LAUNCHER_ROOT "/config.legacy.conf");
  }
  if (!config_json_valid(&s_cfg))
    config_defaults(&s_cfg);

  /* Apply timezone to environment */
  setenv("TZ", s_cfg.timezone, 1);
  tzset();
  ESP_LOGI(TAG, "config loaded, theme=%s tz=%s", s_cfg.theme,
           s_cfg.timezone_id);
}

const launcher_config_t *storage_get_config(void) { return &s_cfg; }
launcher_config_t *storage_get_config_rw(void) { return &s_cfg; }

esp_err_t storage_save(void) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_lock)
    return ESP_ERR_NO_MEM;
  if (!config_json_valid(&s_cfg))
    return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t err = config_write(STORAGE_CFG_PATH, &s_cfg);
  xSemaphoreGive(s_lock);
  return err;
}

esp_err_t storage_reset_appearance(void) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  strncpy(s_cfg.color, "graphite", sizeof(s_cfg.color) - 1);
  strncpy(s_cfg.theme, "graphite", sizeof(s_cfg.theme) - 1);
  strncpy(s_cfg.wallpaper, "", sizeof(s_cfg.wallpaper) - 1);
  s_cfg.wall_dim = 1;
  s_cfg.idle_clock = true;
  s_cfg.clock_style = 0;
  s_cfg.clock_back = 0;
  xSemaphoreGive(s_lock);
  return storage_save();
}

esp_err_t storage_reset_all(void) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  config_defaults(&s_cfg);
  xSemaphoreGive(s_lock);
  return storage_save();
}

void storage_snapshot(launcher_config_t *out) {
  if (!out)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  *out = s_cfg;
  xSemaphoreGive(s_lock);
}

esp_err_t storage_export_json(const char *dest_path) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  launcher_config_t copy;
  storage_snapshot(&copy);
  return config_write(dest_path, &copy);
}

esp_err_t storage_import_json(const char *src_path) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  if (!src_path)
    return ESP_ERR_INVALID_ARG;
  launcher_config_t imported;
  storage_snapshot(&imported);
  esp_err_t err = sd_config_load(&imported, src_path);
  return err == ESP_OK ? storage_update(&imported) : err;
}

esp_err_t storage_update(const launcher_config_t *c) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!c || !config_json_valid(c))
    return ESP_ERR_INVALID_ARG;
  launcher_config_t managed = *c;
  if (managed.wallpaper[0] && strcmp(managed.wallpaper, "-")) {
    char path[sizeof(managed.wallpaper)];
    esp_err_t e =
        launcher_adopt_image(managed.wallpaper, "wall", path, sizeof(path));
    if (e)
      return e;
    snprintf(managed.wallpaper, sizeof(managed.wallpaper), "%s", path);
  }
  c = &managed;
  if (!config_json_valid(c))
    return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t err = config_write(STORAGE_CFG_PATH, c);
  if (err == ESP_OK) {
    s_cfg = *c;
    setenv("TZ", c->timezone, 1);
    tzset();
  }
  xSemaphoreGive(s_lock);
  return err;
}

/* Import appearance data only. Paths and all scalar values pass the normal
 * codec. */
esp_err_t storage_import_theme(const char *path) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f)
    return ESP_ERR_NOT_FOUND;
  char *text = calloc(1, 8193);
  if (!text) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t n = fread(text, 1, 8192, f);
  bool valid = feof(f) && !ferror(f) && !memchr(text, 0, n);
  fclose(f);
  valid = valid && json_depth_valid(text, 4);
  cJSON *root = valid ? cJSON_ParseWithOpts(text, NULL, true) : NULL;
  cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
  valid = cJSON_IsObject(root) && cJSON_IsNumber(schema) &&
          schema->valuedouble == 1;
  for (cJSON *p = root ? root->child : NULL; p; p = p->next) {
    const char *keys[] = {"schema",  "theme",     "color",      "wallpaper",
                          "wallDim", "idleClock", "clockStyle", "clockBack"};
    bool known = false;
    for (unsigned i = 0; i < sizeof(keys) / sizeof(*keys); i++)
      if (p->string && !strcmp(p->string, keys[i]))
        known = true;
    if (!known)
      valid = false;
  }
  launcher_config_t cfg;
  storage_snapshot(&cfg);
  esp_err_t e = valid ? config_json_decode(text, &cfg) : ESP_ERR_INVALID_ARG;
  cJSON_Delete(root);
  free(text);
  return e == ESP_OK ? storage_update(&cfg) : e;
}
