#include "storage_guard.h"
#include "pi_services.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "sd_files.h"
#include "launcher_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static bool meta_size(const char *meta, uint32_t *out) {
  const char *p = meta;
  while (*p) {
    const char *end = strchr(p, '\n');
    if (!end)
      end = p + strlen(p);
    if (!strncmp(p, "size=", 5)) {
      char *stop;
      unsigned long v = strtoul(p + 5, &stop, 10);
      if (stop != end || !v || v > 32 * 1048576)
        return false;
      *out = v;
      return true;
    }
    p = *end ? end + 1 : end;
  }
  return false;
}
esp_err_t pi_services_list(bool backups, char *text, size_t capacity) {
  if (!text || capacity < 2)
    return ESP_ERR_INVALID_ARG;
  size_t n = 0;
  int code = 0;
  char meta[384];
  esp_err_t e = pi_link_call(backups ? "backups" : "apps", NULL, 0, text,
                             capacity - 1, &n, &code, meta, sizeof(meta));
  if (!e && code != 200)
    e = ESP_FAIL;
  if (!e)
    text[n] = 0;
  return e;
}
static esp_err_t read_piece(const char *where, const char *name, uint32_t off,
                            uint32_t len, uint8_t *buf, char meta[384]) {
  char req[384];
  snprintf(req, sizeof(req), "%s %lu %lu %s", where, (unsigned long)off,
           (unsigned long)len, name);
  size_t n = 0;
  int code = 0;
  esp_err_t e =
      pi_link_call("file", req, strlen(req), buf, len, &n, &code, meta, 384);
  return e ? e : code == 200 && n == len ? ESP_OK : ESP_FAIL;
}
esp_err_t pi_services_fetch(const char *where, const char *name, char *path,
                            size_t size, pi_service_progress_fn cb, void *u) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!where || (strcmp(where, "apps") && strcmp(where, "backups")) ||
      !sd_name_valid(name) || strlen(name) > 240 || !path)
    return ESP_ERR_INVALID_ARG;
  char meta[384];
  esp_err_t e = read_piece(where, name, 0, 0, NULL, meta);
  uint32_t total = 0;
  if (e || !meta_size(meta, &total))
    return e ? e : ESP_ERR_INVALID_SIZE;
  mkdir(LAUNCHER_ROOT "/downloads", 0755);
  char dir[512], temp[512];
  snprintf(dir, sizeof(dir), LAUNCHER_ROOT "/downloads/pi-%lld",
           (long long)esp_timer_get_time());
  if (mkdir(dir, 0755))
    return ESP_FAIL;
  if (!sd_path_join(path, size, dir, name) ||
      snprintf(temp, sizeof(temp), "%s.part", path) >= (int)sizeof(temp))
    return ESP_ERR_INVALID_SIZE;
  FILE *f = fopen(temp, "wx");
  if (!f)
    return ESP_FAIL;
  uint8_t *buf = malloc(32768);
  if (!buf)
    e = ESP_ERR_NO_MEM;
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  for (uint32_t off = 0; !e && off < total;) {
    uint32_t n = total - off < 32768 ? total - off : 32768;
    e = read_piece(where, name, off, n, buf, meta);
    if (!e && fwrite(buf, 1, n, f) != n)
      e = ESP_FAIL;
    if (!e) {
      mbedtls_sha256_update(&hash, buf, n);
      off += n;
      if (cb && !cb((uint64_t)off * 100 / total, "Recebendo arquivo do Pi", u))
        e = ESP_ERR_INVALID_STATE;
    }
  }
  uint8_t expected[32], actual[32];
  mbedtls_sha256_finish(&hash, expected);
  if (fflush(f) || fsync(fileno(f)))
    e = ESP_FAIL;
  if (fclose(f))
    e = ESP_FAIL;
  if (!e) {
    f = fopen(temp, "rb");
    if (!f)
      e = ESP_FAIL;
    else {
      mbedtls_sha256_starts(&hash, 0);
      size_t n;
      while ((n = fread(buf, 1, 32768, f)))
        mbedtls_sha256_update(&hash, buf, n);
      if (ferror(f))
        e = ESP_FAIL;
      fclose(f);
      mbedtls_sha256_finish(&hash, actual);
      if (memcmp(actual, expected, 32))
        e = ESP_ERR_INVALID_CRC;
    }
  }
  mbedtls_sha256_free(&hash);
  free(buf);
  if (!e && rename(temp, path))
    e = ESP_FAIL;
  if (e)
    unlink(temp);
  return e;
}
esp_err_t pi_services_backup(const char *local, pi_service_progress_fn cb,
                             void *u) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  if (!sd_path_valid(local))
    return ESP_ERR_INVALID_ARG;
  FILE *f = fopen(local, "rb");
  if (!f)
    return ESP_ERR_NOT_FOUND;
  struct stat st;
  if (fstat(fileno(f), &st) || st.st_size != 16 * 1048576) {
    fclose(f);
    return ESP_ERR_INVALID_SIZE;
  }
  char name[80];
  snprintf(name, sizeof(name), "tab5-%lld.bin",
           (long long)esp_timer_get_time());
  uint8_t *buf = malloc(33000);
  esp_err_t e = buf ? ESP_OK : ESP_ERR_NO_MEM;
  char meta[384];
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  for (uint32_t off = 0; !e && off < (uint32_t)st.st_size; off += 32768) {
    int h = snprintf((char *)buf, 200, "%s %lu %lu\n", name, (unsigned long)off,
                     (unsigned long)st.st_size);
    if (fread(buf + h, 1, 32768, f) != 32768) {
      e = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&hash, buf + h, 32768);
    size_t received = 0;
    int code = 0;
    e = pi_link_call("backup", buf, h + 32768, NULL, 0, &received, &code, meta,
                     sizeof(meta));
    if (!e && code != 200)
      e = ESP_FAIL;
    if (!e && cb &&
        !cb((uint64_t)(off + 32768) * 80 / st.st_size, "Backup no Pi", u))
      e = ESP_ERR_INVALID_STATE;
  }
  fclose(f);
  uint8_t expected[32], actual[32];
  mbedtls_sha256_finish(&hash, expected);
  mbedtls_sha256_starts(&hash, 0);
  for (uint32_t off = 0; !e && off < (uint32_t)st.st_size; off += 32768) {
    e = read_piece("backups", name, off, 32768, buf, meta);
    if (!e)
      mbedtls_sha256_update(&hash, buf, 32768);
    if (!e && cb &&
        !cb(80 + (uint64_t)(off + 32768) * 20 / st.st_size,
            "Verificando backup no Pi", u))
      e = ESP_ERR_INVALID_STATE;
  }
  mbedtls_sha256_finish(&hash, actual);
  mbedtls_sha256_free(&hash);
  free(buf);
  if (!e && memcmp(expected, actual, 32))
    e = ESP_ERR_INVALID_CRC;
  return e;
}
