#include "catalog_tracking.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  char path[512], source[512];
  uint8_t identity[32];
  catalog_item_t item;
} record_t;
static void key(const char *prefix, const void *bytes, size_t size,
                char out[48]) {
  uint8_t h[32];
  mbedtls_sha256(bytes, size, h, 0);
  snprintf(out, 48, "%s-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           prefix, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9],
           h[10], h[11]);
}
static bool valid(const record_t *r) {
  if (!memchr(r->path, 0, sizeof(r->path)) ||
      !memchr(r->source, 0, sizeof(r->source)) ||
      !memchr(r->item.id, 0, sizeof(r->item.id)) ||
      !memchr(r->item.version, 0, sizeof(r->item.version)) ||
      !memchr(r->item.name, 0, sizeof(r->item.name)) ||
      !memchr(r->item.description, 0, sizeof(r->item.description)) ||
      !memchr(r->item.cover_url, 0, sizeof(r->item.cover_url)) ||
      !memchr(r->item.cover_path, 0, sizeof(r->item.cover_path)) ||
      !catalog_https_url(r->source) || r->item.count < 1 ||
      r->item.count > CATALOG_MAX_PARTS)
    return false;
  for (unsigned i = 0; i < r->item.count; i++) {
    const catalog_part_t *p = &r->item.parts[i];
    if (!memchr(p->url, 0, sizeof(p->url)) ||
        !memchr(p->sha256, 0, sizeof(p->sha256)) ||
        !memchr(p->file, 0, sizeof(p->file)) || !catalog_https_url(p->url))
      return false;
  }
  return true;
}
esp_err_t catalog_record_download(const char *path, const catalog_item_t *item,
                                  const char *source) {
  if (!path || !item || !catalog_https_url(source))
    return ESP_ERR_INVALID_ARG;
  record_t *r = calloc(1, sizeof(*r));
  if (!r)
    return ESP_ERR_NO_MEM;
  snprintf(r->path, sizeof(r->path), "%s", path);
  snprintf(r->source, sizeof(r->source), "%s", source);
  r->item = *item;
  char name[48];
  key("download", path, strlen(path), name);
  esp_err_t e = valid(r) ? launcher_store_write(name, r, sizeof(*r))
                         : ESP_ERR_INVALID_ARG;
  free(r);
  return e;
}
static esp_err_t download_read(const char *path, record_t *r) {
  char name[48];
  key("download", path, strlen(path), name);
  size_t n = sizeof(*r);
  esp_err_t e = launcher_store_read(name, r, &n);
  if (e == ESP_ERR_NOT_FOUND && !strncmp(path, LAUNCHER_ROOT "/downloads/",
                                         strlen(LAUNCHER_ROOT "/downloads/"))) {
    char legacy[512];
    snprintf(legacy, sizeof(legacy), "/sdcard/downloads/%s",
             path + strlen(LAUNCHER_ROOT "/downloads/"));
    key("download", legacy, strlen(legacy), name);
    n = sizeof(*r);
    e = launcher_store_read(name, r, &n);
    if (!e && n == sizeof(*r) && valid(r) && !strcmp(r->path, legacy))
      snprintf(r->path, sizeof(r->path), "%s", path);
  }
  if (!e && (n != sizeof(*r) || !valid(r) || strcmp(path, r->path)))
    e = ESP_ERR_INVALID_CRC;
  return e;
}
esp_err_t catalog_alias_download(const char *original, const char *staged) {
  if (!original || !staged)
    return ESP_ERR_INVALID_ARG;
  record_t *r = malloc(sizeof(*r));
  if (!r)
    return ESP_ERR_NO_MEM;
  esp_err_t e = download_read(original, r);
  if (!e)
    e = catalog_record_download(staged, &r->item, r->source);
  free(r);
  return e;
}
esp_err_t catalog_track_install(const char *path, const uint8_t identity[32]) {
  record_t *r = malloc(sizeof(*r));
  if (!r)
    return ESP_ERR_NO_MEM;
  char name[48];
  esp_err_t e = download_read(path, r);
  if (!e) {
    memcpy(r->identity, identity, 32);
    key("origin", identity, 32, name);
    e = launcher_store_write(name, r, sizeof(*r));
  }
  free(r);
  return e;
}
esp_err_t catalog_link_slot(int slot, const catalog_item_t *item,
                            const char *source) {
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  record_t *r = calloc(1, sizeof(*r));
  if (!r)
    return ESP_ERR_NO_MEM;
  esp_app_desc_t desc;
  esp_err_t e =
      p ? esp_ota_get_partition_description(p, &desc) : ESP_ERR_NOT_FOUND;
  if (!e)
    e = esp_partition_get_sha256(p, r->identity);
  if (!e) {
    r->item = *item;
    snprintf(r->item.version, sizeof(r->item.version), "%.31s", desc.version);
    snprintf(r->source, sizeof(r->source), "%s", source);
    char name[48];
    key("origin", r->identity, 32, name);
    e = valid(r) ? launcher_store_write(name, r, sizeof(*r))
                 : ESP_ERR_INVALID_ARG;
  }
  free(r);
  return e;
}
esp_err_t catalog_check_updates(catalog_updates_t *out, catalog_progress_fn cb,
                                void *u) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  record_t **records = calloc(CATALOG_UPDATE_MAX, sizeof(*records));
  catalog_list_t *list = calloc(1, sizeof(*list));
  if (!records || !list) {
    free(records);
    free(list);
    return ESP_ERR_NO_MEM;
  }
  for (unsigned slot = 0; slot < CATALOG_UPDATE_MAX; slot++) {
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
    uint8_t id[32];
    if (!p || esp_partition_get_sha256(p, id))
      continue;
    record_t *r = malloc(sizeof(*r));
    if (!r)
      continue;
    char name[48];
    key("origin", id, 32, name);
    size_t n = sizeof(*r);
    if (launcher_store_read(name, r, &n) || n != sizeof(*r) ||
        memcmp(r->identity, id, 32) || !valid(r)) {
      out->untracked++;
      free(r);
      continue;
    }
    records[slot] = r;
  }
  esp_err_t result = ESP_OK;
  for (unsigned i = 0; i < CATALOG_UPDATE_MAX; i++) {
    if (!records[i])
      continue;
    bool seen = false;
    for (unsigned j = 0; j < i; j++)
      if (records[j] && !strcmp(records[j]->source, records[i]->source))
        seen = true;
    if (seen)
      continue;
    if (cb && !cb(i * 100 / CATALOG_UPDATE_MAX,
                  "Consultando fontes dos apps instalados", u)) {
      result = ESP_ERR_INVALID_STATE;
      break;
    }
    char message[256];
    esp_err_t e = catalog_fetch_latest(records[i]->source, list, message,
                                       sizeof(message), cb, u);
    if (e) {
      out->failed++;
      continue;
    }
    for (unsigned k = i; k < CATALOG_UPDATE_MAX; k++) {
      record_t *r = records[k];
      if (!r || strcmp(r->source, records[i]->source))
        continue;
      for (unsigned j = 0; j < list->count; j++)
        if (!strcmp(r->item.id, list->items[j].id)) {
          catalog_update_t *row = &out->rows[out->count++];
          row->slot = k;
          snprintf(row->previous, sizeof(row->previous), "%s", r->item.version);
          snprintf(row->source, sizeof(row->source), "%s", r->source);
          row->item = list->items[j];
          row->changed = strcmp(row->previous, row->item.version) != 0;
          if (!row->changed && r->item.count == row->item.count)
            for (unsigned z = 0; z < row->item.count; z++)
              if (r->item.parts[z].sha256[0] && row->item.parts[z].sha256[0] &&
                  strcmp(r->item.parts[z].sha256, row->item.parts[z].sha256))
                row->changed = true;
          break;
        }
    }
  }
  for (unsigned i = 0; i < CATALOG_UPDATE_MAX; i++)
    free(records[i]);
  free(records);
  free(list);
  return result;
}
esp_err_t catalog_download_updates(const catalog_updates_t *updates,
                                   catalog_progress_fn cb, void *u, char *m,
                                   size_t n) {
  if (!updates)
    return ESP_ERR_INVALID_ARG;
  unsigned done = 0, failed = 0;
  char path[512];
  for (unsigned i = 0; i < updates->count; i++)
    if (updates->rows[i].changed) {
      if (cb && !cb(i * 100 / (updates->count ? updates->count : 1),
                    "Baixando atualizações; instalação exige confirmação", u))
        return ESP_ERR_INVALID_STATE;
      esp_err_t e =
          catalog_download_from(&updates->rows[i].item, updates->rows[i].source,
                                path, sizeof(path), cb, u);
      if (e)
        failed++;
      else
        done++;
    }
  snprintf(m, n,
           "%u atualização(ões) baixadas; %u falha(s). Arquivos mantidos para "
           "retomada; confirme instalação pelo SD.",
           done, failed);
  return failed ? ESP_FAIL : ESP_OK;
}
