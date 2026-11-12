#include "storage_guard.h"
#include "catalog.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define MAGIC 0x32434154u
typedef struct {
  uint32_t magic, item_size, count;
  int64_t created;
  char source[512];
  uint8_t digest[32];
} header_t;
static const char *canonical(const char *u) {
  return u && !strncmp(u, "https://bmorcelli.github.io/Launcher/catalog.html",
                       46)
             ? "https://api.launcherhub.net/giveMeTheList"
             : u;
}
static bool path_for(const char *u, char *out, size_t n) {
  u = canonical(u);
  if (!catalog_https_url(u))
    return false;
  uint8_t sha[32];
  mbedtls_sha256((const uint8_t *)u, strlen(u), sha, 0);
  char hex[33];
  for (unsigned i = 0; i < 16; i++)
    snprintf(hex + 2 * i, 3, "%02x", sha[i]);
  snprintf(out, n, LAUNCHER_ROOT "/cache/catalog-%s.cache", hex);
  return true;
}
esp_err_t catalog_cache_read(const char *source, catalog_list_t *out,
                             int64_t *age) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  char path[192];
  if (!out || !path_for(source, path, sizeof(path)))
    return ESP_ERR_INVALID_ARG;
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_ERR_NOT_FOUND;
  header_t h;
  bool ok = fread(&h, 1, sizeof(h), f) == sizeof(h) && h.magic == MAGIC &&
            h.item_size == sizeof(catalog_item_t) &&
            h.count <= CATALOG_MAX_ITEMS &&
            memchr(h.source, 0, sizeof(h.source)) &&
            !strcmp(h.source, canonical(source));
  size_t size = ok ? h.count * sizeof(catalog_item_t) : 0;
  if (ok)
    ok = fread(out->items, 1, size, f) == size && fgetc(f) == EOF && !ferror(f);
  fclose(f);
  uint8_t digest[32];
  if (ok) {
    mbedtls_sha256((const uint8_t *)out->items, size, digest, 0);
    ok = !memcmp(digest, h.digest, 32);
  }
  if (!ok) {
    out->count = 0;
    return ESP_ERR_INVALID_CRC;
  }
  for (unsigned i = 0; i < h.count; i++) {
    catalog_item_t *p = &out->items[i];
    if (!memchr(p->id, 0, sizeof(p->id)) ||
        !memchr(p->cover_path, 0, sizeof(p->cover_path)) ||
        !memchr(p->name, 0, sizeof(p->name)) ||
        !memchr(p->description, 0, sizeof(p->description)) ||
        !memchr(p->cover_url, 0, sizeof(p->cover_url)) ||
        !memchr(p->version, 0, sizeof(p->version)) || p->count < 1 ||
        p->count > CATALOG_MAX_PARTS) {
      out->count = 0;
      return ESP_ERR_INVALID_CRC;
    }
  }
  for (unsigned i = 0; i < h.count; i++)
    for (unsigned j = 0; j < out->items[i].count; j++) {
      catalog_part_t *p = &out->items[i].parts[j];
      if (!memchr(p->file, 0, sizeof(p->file)) ||
          !memchr(p->url, 0, sizeof(p->url)) ||
          !memchr(p->sha256, 0, sizeof(p->sha256)) ||
          !catalog_https_url(p->url)) {
        out->count = 0;
        return ESP_ERR_INVALID_CRC;
      }
    }
  out->count = h.count;
  if (age)
    *age = time(NULL) > 1704067200 && h.created > 1704067200
               ? time(NULL) - h.created
               : -1;
  return ESP_OK;
}
esp_err_t catalog_cache_write(const char *source, const catalog_list_t *list) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  char path[192], tmp[208];
  if (!list || list->count > CATALOG_MAX_ITEMS ||
      !path_for(source, path, sizeof(path)))
    return ESP_ERR_INVALID_ARG;
  if (launcher_store_init() != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  mkdir(LAUNCHER_ROOT "/cache", 0755);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return ESP_FAIL;
  header_t h = {.magic = MAGIC,
                .item_size = sizeof(catalog_item_t),
                .count = list->count,
                .created = time(NULL)};
  snprintf(h.source, sizeof(h.source), "%s", canonical(source));
  size_t size = list->count * sizeof(catalog_item_t);
  mbedtls_sha256((const uint8_t *)list->items, size, h.digest, 0);
  bool ok = fwrite(&h, 1, sizeof(h), f) == sizeof(h) &&
            fwrite(list->items, 1, size, f) == size;
  if (fflush(f) || fsync(fileno(f)))
    ok = false;
  if (fclose(f))
    ok = false;
  /* Only derived metadata is replaced, never a firmware/configuration file. */
  if (ok && rename(tmp, path)) {
    unlink(path);
    ok = rename(tmp, path) == 0;
  }
  if (!ok)
    unlink(tmp);
  return ok ? ESP_OK : ESP_FAIL;
}
