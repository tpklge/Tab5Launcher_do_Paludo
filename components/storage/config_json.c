#include "config_json.h"
#include "theme_catalog.h"
#include "cJSON.h"
#include "json_limits.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum { U8, U16, BOOL, STRING } field_type;
typedef struct {
  const char *name;
  size_t offset, size;
  field_type type;
  unsigned min, max;
} field;
#define NUM(key, member, type, lo, hi)                                         \
  {key,                                                                        \
   offsetof(launcher_config_t, member),                                        \
   sizeof(((launcher_config_t *)0)->member),                                   \
   type,                                                                       \
   lo,                                                                         \
   hi}
#define STR(key, member) NUM(key, member, STRING, 0, 0)
static const field fields[] = {
    NUM("bright", bright, U8, 5, 100),
    NUM("dim", dim_s, U16, 0, 65535),
    NUM("screenOff", screen_off_s, U16, 0, 65535),
    STR("color", color),
    STR("theme", theme),
    NUM("rot", rot, U16, 0, 180),
    NUM("askSpiffs", ask_spiffs, BOOL, 0, 1),
    NUM("onlyBins", only_bins, BOOL, 0, 1),
    NUM("bootTime", boot_time_s, U8, 1, 10),
    NUM("wifiAuto", wifi_autoconnect, BOOL, 0, 1),
    NUM("deepSleepStart", deep_sleep_start, BOOL, 0, 1),
    NUM("buttonOnly", btn_only_start, BOOL, 0, 1),
    STR("timezone", timezone),
    STR("timeZone", timezone_id),
    NUM("idleClock", idle_clock, BOOL, 0, 1),
    NUM("clockStyle", clock_style, U8, 0, 3),
    NUM("clockBack", clock_back, U8, 0, 2),
    NUM("wallDim", wall_dim, U8, 0, 2),
    STR("wallpaper", wallpaper),
    NUM("powerLogging", power_logging, BOOL, 0, 1),
    NUM("batteryWarning", battery_warning, BOOL, 0, 1),
    NUM("criticalShutdown", critical_shutdown, BOOL, 0, 1),
    NUM("piEnabled", pi_enabled, BOOL, 0, 1),
    NUM("piAutoBackup", pi_auto_backup, BOOL, 0, 1)};
bool config_json_valid(const launcher_config_t *c) {
  if (!c)
    return false;
  for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
    const field *f = &fields[i];
    const unsigned char *p = (const unsigned char *)c + f->offset;
    if (f->type == STRING) {
      if (!memchr(p, 0, f->size))
        return false;
      for (const unsigned char *q = p; *q; q++)
        if (*q < 32 || *q == 127)
          return false;
    } else {
      unsigned n = f->type == U16    ? *(const uint16_t *)p
                   : f->type == BOOL ? *(const bool *)p
                                     : *p;
      if (n < f->min || n > f->max)
        return false;
    }
  }
  return (c->rot == 0 || c->rot == 180) &&
         launcher_theme_valid(c->theme) &&
         memchr(c->scheme, 0, sizeof(c->scheme));
}
esp_err_t config_json_decode(const char *text, launcher_config_t *c) {
  if (!text || !c || strlen(text) > 8192 || !json_depth_valid(text, 4) ||
      strstr(text, "\\u0000"))
    return ESP_ERR_INVALID_ARG;
  cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
  }
  launcher_config_t candidate = *c;
  bool valid = true;
  cJSON *item;
  cJSON_ArrayForEach(item, root) {
    for (cJSON *prior = root->child; prior != item; prior = prior->next)
      if (!strcmp(prior->string, item->string))
        valid = false;
    if (!strcmp(item->string, "schema")) {
      if (!cJSON_IsNumber(item) || item->valuedouble != 1)
        valid = false;
      continue;
    }
    /* Legacy exported scheme is informational, never imported. */
    if (!strcmp(item->string, "scheme")) {
      if (!cJSON_IsString(item))
        valid = false;
      continue;
    }
    const field *f = NULL;
    for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); i++)
      if (!strcmp(fields[i].name, item->string))
        f = &fields[i];
    if (!f) {
      valid = false;
      continue;
    }
    unsigned char *p = (unsigned char *)&candidate + f->offset;
    if (f->type == STRING) {
      if (!cJSON_IsString(item) || strlen(item->valuestring) >= f->size)
        valid = false;
      else {
        memset(p, 0, f->size);
        strcpy((char *)p, item->valuestring);
      }
    } else if (f->type == BOOL) {
      if (!cJSON_IsBool(item))
        valid = false;
      else
        *(bool *)p = cJSON_IsTrue(item);
    } else {
      double n = item->valuedouble;
      if (!cJSON_IsNumber(item) || !isfinite(n) || n < f->min || n > f->max ||
          n != (unsigned)n)
        valid = false;
      else if (f->type == U16)
        *(uint16_t *)p = (uint16_t)n;
      else
        *p = (uint8_t)n;
    }
  }
  valid = valid && config_json_valid(&candidate);
  cJSON_Delete(root);
  if (!valid)
    return ESP_ERR_INVALID_ARG;
  *c = candidate;
  return ESP_OK;
}
char *config_json_encode(const launcher_config_t *c) {
  if (!config_json_valid(c))
    return NULL;
  cJSON *root = cJSON_CreateObject();
  if (!root)
    return NULL;
  bool ok = cJSON_AddNumberToObject(root, "schema", 1) != NULL;
  for (unsigned i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
    const field *f = &fields[i];
    const unsigned char *p = (const unsigned char *)c + f->offset;
    cJSON *added =
        f->type == STRING
            ? cJSON_AddStringToObject(root, f->name, (const char *)p)
        : f->type == BOOL
            ? cJSON_AddBoolToObject(root, f->name, *(const bool *)p)
            : cJSON_AddNumberToObject(
                  root, f->name, f->type == U16 ? *(const uint16_t *)p : *p);
    if (!added)
      ok = false;
  }
  char *out = ok ? cJSON_Print(root) : NULL;
  cJSON_Delete(root);
  return out;
}
