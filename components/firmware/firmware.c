/* Strict app, checked merged table, explicit multipart manifest and own slot
 * backups. */
#include "firmware.h"
#include "cJSON.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "json_limits.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "sd_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {
  FILE *file;
  uint32_t base, length;
} reader_t;
static uint32_t le32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static bool read_at(void *ctx, uint32_t off, void *out, size_t n) {
  reader_t *r = ctx;
  return off <= r->length && n <= r->length - off &&
         fseek(r->file, r->base + off, SEEK_SET) == 0 &&
         fread(out, 1, n, r->file) == n;
}
static bool digest(reader_t *r, uint32_t length, uint8_t out[32]) {
  if (length > r->length || fseek(r->file, r->base, SEEK_SET))
    return false;
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  int err = mbedtls_sha256_starts(&h, 0);
  uint8_t *b = malloc(4096);
  if (!b) {
    mbedtls_sha256_free(&h);
    return false;
  }
  unsigned chunks = 0;
  while (!err && length) {
    size_t n = length < 4096 ? length : 4096;
    if (fread(b, 1, n, r->file) != n) {
      err = -1;
      break;
    }
    err = mbedtls_sha256_update(&h, b, n);
    length -= n;
    if (++chunks % 32 == 0)
      vTaskDelay(1);
  }
  if (!err)
    err = mbedtls_sha256_finish(&h, out);
  mbedtls_sha256_free(&h);
  free(b);
  return !err;
}
static bool check_hash(void *ctx, uint32_t n, const uint8_t expected[32]) {
  uint8_t d[32];
  return digest(ctx, n, d) && !memcmp(d, expected, 32);
}
esp_err_t firmware_hash_region(const firmware_region_t *region,
                               uint8_t out[32]) {
  FILE *f = fopen(region->path, "rb");
  if (!f)
    return ESP_FAIL;
  struct stat st;
  bool ok = fstat(fileno(f), &st) == 0 &&
            region->offset <= (uint64_t)st.st_size &&
            region->length <= (uint64_t)st.st_size - region->offset;
  reader_t r = {f, region->offset, region->length};
  if (ok)
    ok = digest(&r, r.length, out);
  fclose(f);
  return ok ? ESP_OK : ESP_FAIL;
}
static bool parse_app(reader_t *r, firmware_info_t *info, bool padded,
                      char *error, size_t size) {
  uint8_t header[24], segment[8];
  if (!read_at(r, 0, header, 24) || header[0] != 0xe9 || !header[1] ||
      header[1] > 16)
    return false;
  uint32_t off = 24;
  for (unsigned i = 0; i < header[1]; i++) {
    if (!read_at(r, off, segment, 8))
      return false;
    off += 8;
    uint32_t n = le32(segment + 4);
    if (n > r->length - off)
      return false;
    off += n;
  }
  uint32_t length = ((off + 16) & ~15u) + (header[23] ? 32 : 0);
  if (length > r->length || (!padded && length != r->length))
    return false;
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  if (!firmware_parse(r, read_at, check_hash, length, chip.revision,
                      &info->image, error, size))
    return false;
  info->app.offset = r->base;
  info->app.length = info->kind == FW_RAW_BACKUP ? r->length : length;
  if (!digest(r, info->app.length, info->sha256))
    return false;
  memcpy(info->app.sha256, info->sha256, 32);
  return true;
}
static cJSON *read_json(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  char *buf = malloc(16385);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, 16385, f);
  bool ok = !ferror(f) && n < 16385;
  fclose(f);
  cJSON *j = NULL;
  if (ok) {
    buf[n] = 0;
    const char *end = NULL;
    if (!memchr(buf, 0, n) && json_depth_valid(buf, 8) &&
        !strstr(buf, "\\u0000"))
      j = cJSON_ParseWithLengthOpts(buf, n + 1, &end, true);
  }
  free(buf);
  return j;
}
static const char *string(cJSON *j, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool hash_string(const char *hex, uint8_t out[32]) {
  if (!hex || strlen(hex) != 64)
    return false;
  for (int i = 0; i < 32; i++) {
    unsigned v = 0;
    for (int k = 0; k < 2; k++) {
      char c = hex[2 * i + k];
      unsigned n = c >= '0' && c <= '9'   ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : 16;
      if (n > 15)
        return false;
      v = v * 16 + n;
    }
    out[i] = v;
  }
  return true;
}
static bool fields(cJSON *object, const char *allowed) {
  if (!cJSON_IsObject(object))
    return false;
  unsigned count = 0;
  for (cJSON *item = object->child; item; item = item->next) {
    if (!item->string || ++count > 8 || strlen(item->string) > 32)
      return false;
    char key[40];
    snprintf(key, sizeof(key), ",%s,", item->string);
    if (!strstr(allowed, key))
      return false;
    for (cJSON *previous = object->child; previous != item;
         previous = previous->next)
      if (!strcmp(previous->string, item->string))
        return false;
  }
  return true;
}
static bool component(cJSON *j, const char *folder, firmware_region_t *r) {
  if (!fields(j, ",file,sha256,label,"))
    return false;
  const char *name = string(j, "file");
  uint8_t expected[32];
  if (!name || !sd_path_join(r->path, sizeof(r->path), folder, name) ||
      !hash_string(string(j, "sha256"), expected))
    return false;
  struct stat st;
  if (stat(r->path, &st) || st.st_size <= 0 || st.st_size > FW_MAX_FILE_SIZE)
    return false;
  r->length = st.st_size;
  return firmware_hash_region(r, r->sha256) == ESP_OK &&
         !memcmp(expected, r->sha256, 32);
}
static bool manifest(const char *path, firmware_info_t *info, char *error,
                     size_t size) {
  cJSON *j = read_json(path);
  if (!j)
    return false;
  const char *chip = string(j, "chip");
  cJSON *schema = cJSON_GetObjectItemCaseSensitive(j, "schema");
  bool ok = fields(j, ",schema,chip,app,data,") && cJSON_IsNumber(schema) &&
            schema->valuedouble == 1 && chip && !strcmp(chip, "esp32p4");
  char folder[512];
  snprintf(folder, sizeof(folder), "%s", path);
  char *slash = strrchr(folder, '/');
  if (!slash)
    ok = false;
  else
    *slash = 0;
  info->kind = FW_PACKAGE;
  if (ok)
    ok = fields(cJSON_GetObjectItemCaseSensitive(j, "app"), ",file,sha256,") &&
         component(cJSON_GetObjectItemCaseSensitive(j, "app"), folder,
                   &info->app);
  cJSON *data = cJSON_GetObjectItemCaseSensitive(j, "data");
  if (ok && data &&
      (!cJSON_IsArray(data) || cJSON_GetArraySize(data) > FW_DATA_MAX))
    ok = false;
  cJSON *item;
  cJSON_ArrayForEach(item, data) {
    if (!ok)
      break;
    firmware_region_t *r = &info->data[info->data_count];
    const char *label = string(item, "label");
    if (!label || (strcmp(label, "vfs") && strcmp(label, "spiffs"))) {
      ok = false;
      break;
    }
    for (unsigned i = 0; i < info->data_count; i++)
      if (!strcmp(label, info->data[i].label))
        ok = false;
    if (ok)
      ok = component(item, folder, r);
    if (ok) {
      snprintf(r->label, sizeof(r->label), "%s", label);
      info->data_count++;
    }
  }
  if (ok) {
    FILE *f = fopen(info->app.path, "rb");
    if (!f)
      ok = false;
    else {
      reader_t r = {f, 0, info->app.length};
      ok = parse_app(&r, info, false, error, size);
      fclose(f);
    }
  }
  cJSON_Delete(j);
  return ok;
}
static bool merged(reader_t *file, const char *path, firmware_info_t *info,
                   char *error, size_t size) {
  uint8_t table[3072], expected[16];
  if (!read_at(file, 0x8000, table, sizeof(table)))
    return false;
  unsigned entries = 0;
  bool checked = false;
  for (unsigned off = 0; off < sizeof(table); off += 32) {
    uint8_t *e = table + off;
    if (e[0] == 0xeb && e[1] == 0xeb) {
      checked = mbedtls_md5(table, off, expected) == 0 &&
                !memcmp(expected, e + 16, 16);
      break;
    }
    if (e[0] != 0xaa || e[1] != 0x50)
      return false;
    uint32_t addr = le32(e + 4), len = le32(e + 8);
    if (le32(e + 28) || !len || addr < 0x9000 || addr > FW_MAX_FILE_SIZE ||
        len > FW_MAX_FILE_SIZE - addr || (addr & 4095) || (len & 4095))
      return false;
    for (unsigned i = 0; i < entries; i++) {
      uint32_t a = le32(table + i * 32 + 4), n = le32(table + i * 32 + 8);
      if (addr < a + n && a < addr + len)
        return false;
    }
    entries++;
  }
  if (!checked)
    return false;
  unsigned apps = 0;
  info->kind = FW_MERGED;
  for (unsigned i = 0; i < entries; i++) {
    uint8_t *e = table + i * 32;
    uint32_t addr = le32(e + 4), len = le32(e + 8);
    if (addr >= file->length)
      continue;
    if (e[2] == 0 && (e[3] == 0 || (e[3] >= 16 && e[3] < 32))) {
      uint8_t magic;
      if (!read_at(file, addr, &magic, 1) || magic != 0xe9)
        continue;
      if (++apps > 1 || (addr & 65535))
        return false;
      reader_t r = {file->file, addr,
                    file->length - addr < len ? file->length - addr : len};
      if (!parse_app(&r, info, true, error, size))
        return false;
      snprintf(info->app.path, sizeof(info->app.path), "%s", path);
    } else if (e[2] == 1 && (e[3] == 0x81 || e[3] == 0x82) &&
               len > file->length - addr) {
      return false;
    } else if (e[2] == 1 && (e[3] == 0x81 || e[3] == 0x82) &&
               len <= file->length - addr) {
      char label[17];
      memcpy(label, e + 12, 16);
      label[16] = 0;
      if (strcmp(label, "vfs") && strcmp(label, "spiffs"))
        return false;
      if (info->data_count == FW_DATA_MAX)
        return false;
      firmware_region_t *r = &info->data[info->data_count++];
      snprintf(r->path, sizeof(r->path), "%s", path);
      snprintf(r->label, sizeof(r->label), "%s", label);
      r->offset = addr;
      r->length = len;
      reader_t source = {file->file, addr, len};
      if (!digest(&source, len, r->sha256))
        return false;
    }
  }
  return apps == 1;
}
esp_err_t firmware_init(void) { return ESP_OK; }
static esp_err_t analyze_impl(const char *path, firmware_info_t *info,
                              char *error, size_t size) {
  if (!path || !info || !sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  memset(info, 0, sizeof(*info));
  if (error && size)
    error[0] = 0;
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_FAIL;
  struct stat st;
  bool ok = fstat(fileno(f), &st) == 0 && st.st_size > 0 &&
            st.st_size <= FW_MAX_FILE_SIZE;
  reader_t r = {f, 0, ok ? (uint32_t)st.st_size : 0};
  if (ok)
    ok = digest(&r, r.length, info->source_sha256);
  const char *ext = strrchr(path, '.');
  if (ok && ext && !strcmp(ext, ".json")) {
    fclose(f);
    f = NULL;
    ok = manifest(path, info, error, size);
  } else if (ok && ext && !strcmp(ext, ".raw")) {
    char sidecar[520];
    snprintf(sidecar, sizeof(sidecar), "%s.json", path);
    cJSON *j = read_json(sidecar);
    const char *kind = j ? string(j, "kind") : NULL;
    uint8_t sha[32];
    cJSON *n = j ? cJSON_GetObjectItemCaseSensitive(j, "size") : NULL;
    cJSON *schema = j ? cJSON_GetObjectItemCaseSensitive(j, "schema") : NULL;
    ok = fields(j, ",schema,kind,offset,size,sha256,") &&
         cJSON_IsNumber(schema) && schema->valuedouble == 1 && kind &&
         !strcmp(kind, "partition-backup") && cJSON_IsNumber(n) &&
         n->valuedouble == st.st_size &&
         hash_string(string(j, "sha256"), sha) &&
         !memcmp(sha, info->source_sha256, 32);
    cJSON *addr = j ? cJSON_GetObjectItemCaseSensitive(j, "offset") : NULL;
    if (ok)
      ok = cJSON_IsNumber(addr) &&
           (addr->valuedouble == 0x210000 || addr->valuedouble == 0x610000 ||
            addr->valuedouble == 0xa10000 || addr->valuedouble == 0xf16000);
    if (ok &&
        (addr->valuedouble == 0xa10000 || addr->valuedouble == 0xf16000)) {
      info->kind = FW_DATA_BACKUP;
      info->data_count = 1;
      firmware_region_t *region = &info->data[0];
      snprintf(region->path, sizeof(region->path), "%s", path);
      region->length = st.st_size;
      memcpy(region->sha256, info->source_sha256, 32);
      snprintf(region->label, sizeof(region->label), "%s",
               addr->valuedouble == 0xa10000 ? "vfs" : "spiffs");
      snprintf(info->image.project, sizeof(info->image.project), "Restaurar %s",
               region->label);
      info->image.image_size = region->length;
    } else {
      info->kind = FW_RAW_BACKUP;
      if (ok) {
        snprintf(info->app.label, sizeof(info->app.label), "%s",
                 addr->valuedouble == 0x210000 ? "app0" : "app1");
        ok = parse_app(&r, info, true, error, size);
      }
    }
    cJSON_Delete(j);
    snprintf(info->app.path, sizeof(info->app.path), "%s", path);
  } else if (ok) {
    uint8_t magic;
    ok = read_at(&r, 0, &magic, 1);
    if (ok && magic == 0xe9) {
      snprintf(info->app.path, sizeof(info->app.path), "%s", path);
      ok = parse_app(&r, info, false, error, size);
    } else if (ok)
      ok = merged(&r, path, info, error, size);
  }
  if (f)
    fclose(f);
  if (!ok && error && size && !error[0])
    snprintf(error, size,
             "Imagem, tabela, manifesto ou backup invalido/incompativel");
  return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t firmware_analyze_file(const char *path, firmware_info_t *info,
                                char *error, size_t n) {
  if (!sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  if (!sd_media_acquire(path))
    return ESP_ERR_INVALID_STATE;
  esp_err_t e = analyze_impl(path, info, error, n);
  sd_media_release(path);
  return e;
}
