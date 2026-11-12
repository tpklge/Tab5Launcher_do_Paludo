#include "launcher_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_files.h"
#include "storage_guard.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define RECORD_MAGIC 0x3144534c
static SemaphoreHandle_t s_lock;
static esp_err_t recover_locked(const char *, launcher_file_validator_fn,
                                void *);
static esp_err_t record_valid(const char *, void *);
static uint32_t crc32(const void *bytes, size_t n) {
  const uint8_t *p = bytes;
  uint32_t crc = ~0u;
  while (n--) {
    crc ^= *p++;
    for (unsigned b = 0; b < 8; b++)
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
  }
  return ~crc;
}
static bool key_valid(const char *s) {
  if (!s || !*s || strlen(s) > 48)
    return false;
  for (; *s; s++)
    if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
      return false;
  return true;
}
static bool directory(const char *p) {
  if (!mkdir(p, 0700))
    return true;
  struct stat st;
  return errno == EEXIST && !stat(p, &st) && S_ISDIR(st.st_mode);
}
esp_err_t launcher_store_init(void) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_lock)
    s_lock = xSemaphoreCreateMutex();
  if (!s_lock)
    return ESP_ERR_NO_MEM;
  if (!directory(LAUNCHER_ROOT) || !directory(LAUNCHER_PRIVATE) ||
      !directory(LAUNCHER_WEB) || !directory(LAUNCHER_BACKUPS) ||
      !directory(LAUNCHER_DOWNLOADS) || !directory(LAUNCHER_INCOMING) ||
      !directory(LAUNCHER_DIAGNOSTICS))
    return ESP_ERR_INVALID_STATE;
  return ESP_OK;
}
static esp_err_t read_file(const char *path, void *data, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  uint32_t header[3];
  esp_err_t e = ESP_ERR_INVALID_CRC;
  if (fread(header, 1, sizeof(header), f) == sizeof(header) &&
      header[0] == RECORD_MAGIC && header[1] <= 65536 && header[1] <= *size) {
    size_t n = header[1];
    if (fread(data, 1, n, f) == n && fgetc(f) == EOF && !ferror(f) &&
        crc32(data, n) == header[2]) {
      *size = n;
      e = ESP_OK;
    }
  }
  fclose(f);
  return e;
}
esp_err_t launcher_store_read(const char *key, void *data, size_t *size) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  if (!key_valid(key) || !data || !size)
    return ESP_ERR_INVALID_ARG;
  if (!s_lock && launcher_store_init() != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  struct stat st;
  if (stat(LAUNCHER_PRIVATE, &st))
    return ESP_ERR_INVALID_STATE;
  char path[160], previous[176];
  snprintf(path, sizeof(path), LAUNCHER_PRIVATE "/%s.dat", key);
  snprintf(previous, sizeof(previous), "%s.previous", path);
  xSemaphoreTake(s_lock, portMAX_DELAY);
  size_t capacity = *size;
  esp_err_t e = read_file(path, data, size);
  if (e != ESP_OK) {
    *size = capacity;
    esp_err_t old = read_file(previous, data, size);
    if (old == ESP_OK) {
      e = ESP_OK;
      /* A readable fallback remains useful even on a read-only card. */
      if (storage_write_acquire()) {
        recover_locked(path, record_valid, NULL);
        storage_write_release();
      }
    }
  }
  xSemaphoreGive(s_lock);
  return e;
}
/* All publication/recovery steps below run under s_lock. Recovery never
 * deletes an unverified candidate or overwrites a retained recovery copy. */
static esp_err_t present(const char *path, bool *exists) {
  struct stat st;
  if (!stat(path, &st)) {
    *exists = true;
    return S_ISREG(st.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
  }
  *exists = false;
  return errno == ENOENT ? ESP_OK : ESP_FAIL;
}
static esp_err_t archive(const char *path) {
  char dest[600];
  for (unsigned i = 1; i <= 1024; i++) {
    if (snprintf(dest, sizeof(dest), "%s.recovered-%u", path, i) >=
        sizeof(dest))
      return ESP_ERR_INVALID_SIZE;
    struct stat st;
    if (!stat(dest, &st))
      continue;
    if (errno != ENOENT)
      return ESP_FAIL;
    return rename(path, dest) ? ESP_FAIL : ESP_OK;
  }
  return ESP_ERR_INVALID_STATE;
}
static esp_err_t readable(const char *path, void *unused) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_FAIL;
  uint8_t bytes[512];
  size_t count = 0, n;
  while ((n = fread(bytes, 1, sizeof(bytes), f))) {
    count += n;
    if (count > 131072) {
      fclose(f);
      return ESP_ERR_INVALID_SIZE;
    }
  }
  bool ok = !ferror(f);
  fclose(f);
  return ok ? ESP_OK : ESP_FAIL;
}
static esp_err_t record_valid(const char *path, void *unused) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_FAIL;
  uint32_t head[3], crc = ~0u;
  uint8_t b[512];
  bool ok = fread(head, 1, 12, f) == 12 && head[0] == RECORD_MAGIC &&
            head[1] <= 65536;
  size_t left = ok ? head[1] : 0;
  while (ok && left) {
    size_t n = left < sizeof(b) ? left : sizeof(b);
    ok = fread(b, 1, n, f) == n;
    for (size_t i = 0; ok && i < n; i++) {
      crc ^= b[i];
      for (unsigned j = 0; j < 8; j++)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    left -= n;
  }
  ok = ok && ~crc == head[2] && fgetc(f) == EOF && !ferror(f);
  fclose(f);
  return ok ? ESP_OK : ESP_ERR_INVALID_CRC;
}
static esp_err_t recover_locked(const char *path,
                                launcher_file_validator_fn valid, void *ctx) {
  char old[544], tmp[544];
  snprintf(old, sizeof(old), "%s.previous", path);
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  bool primary = false, previous = false, temporary = false;
  esp_err_t e = present(path, &primary);
  if (e)
    return e;
  if ((e = present(old, &previous)) || (e = present(tmp, &temporary)))
    return e;
  /* Raw files have no intrinsic schema. Prefer the retained committed version
   * rather than guessing that an interrupted publication is valid. */
  bool current_ok = primary && valid && valid(path, ctx) == ESP_OK;
  if (previous) {
    if (current_ok) {
      if ((e = archive(old)))
        return e;
    } else {
      if ((valid ? valid : readable)(old, ctx) != ESP_OK)
        return ESP_ERR_INVALID_CRC;
      if (primary && (e = archive(path)))
        return e;
      if (rename(old, path))
        return ESP_FAIL;
      primary = true;
      current_ok = true;
    }
  }
  if (!primary && temporary) {
    if ((valid ? valid : readable)(tmp, ctx) != ESP_OK)
      return ESP_ERR_INVALID_CRC;
    if (rename(tmp, path))
      return ESP_FAIL;
    primary = true;
    current_ok = true;
    temporary = false;
  }
  if (temporary && (e = archive(tmp)))
    return e;
  if (!primary)
    return ESP_ERR_NOT_FOUND;
  return valid && !current_ok ? valid(path, ctx) : ESP_OK;
}
static esp_err_t publish(const char *path, const void *data, size_t size,
                         launcher_file_validator_fn valid, void *ctx) {
  char tmp[544], old[544];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  snprintf(old, sizeof(old), "%s.previous", path);
  bool had_old = false, had_tmp = false;
  esp_err_t e = present(old, &had_old);
  if (e)
    return e;
  if ((e = present(tmp, &had_tmp)))
    return e;
  if (had_old || had_tmp) {
    /* Only the managed tree owns these legacy recovery filenames. */
    if (strncmp(path, LAUNCHER_ROOT "/", strlen(LAUNCHER_ROOT) + 1))
      return ESP_ERR_INVALID_STATE;
    e = recover_locked(path, valid, ctx);
    if (e != ESP_OK && e != ESP_ERR_NOT_FOUND)
      return e;
  }
  FILE *f = fopen(tmp, "wx");
  if (!f)
    return ESP_FAIL;
  bool ok = fwrite(data, 1, size, f) == size;
  if (fflush(f) || fsync(fileno(f)))
    ok = false;
  if (fclose(f))
    ok = false;
  if (!ok) {
    unlink(tmp);
    return ESP_FAIL;
  }
  if (valid && valid(tmp, ctx) != ESP_OK) {
    unlink(tmp);
    return ESP_ERR_INVALID_CRC;
  }
  bool exists = false;
  if ((e = present(path, &exists))) {
    unlink(tmp);
    return e;
  }
  if (exists && rename(path, old)) {
    unlink(tmp);
    return ESP_FAIL;
  }
  if (rename(tmp, path)) {
    if (exists)
      rename(old, path);
    return ESP_FAIL;
  }
  f = fopen(path, "rb");
  uint8_t chunk[512];
  size_t used = 0;
  ok = f != NULL;
  while (ok && used < size) {
    size_t n = size - used;
    if (n > sizeof(chunk))
      n = sizeof(chunk);
    if (fread(chunk, 1, n, f) != n ||
        memcmp(chunk, (const uint8_t *)data + used, n))
      ok = false;
    used += n;
  }
  if (f) {
    if (fgetc(f) != EOF || ferror(f))
      ok = false;
    fclose(f);
  }
  if (!ok)
    return ESP_ERR_INVALID_CRC; /* Both candidates retained for recovery. */
  if (exists && unlink(old))
    return ESP_FAIL;
  return ESP_OK;
}
esp_err_t launcher_file_recover(const char *path,
                                launcher_file_validator_fn valid, void *ctx) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!path || strlen(path) > 500 || !sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  if (!s_lock && launcher_store_init() != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t e = recover_locked(path, valid, ctx);
  xSemaphoreGive(s_lock);
  return e;
}
esp_err_t launcher_file_write_validated(const char *path, const void *data,
                                        size_t size,
                                        launcher_file_validator_fn valid,
                                        void *ctx) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!path || !data || size > 131072 || strlen(path) > 500 ||
      !sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  if (launcher_store_init() != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t e = publish(path, data, size, valid, ctx);
  xSemaphoreGive(s_lock);
  return e;
}
esp_err_t launcher_file_write(const char *path, const void *data, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!path || !data || size > 131072 ||
      strncmp(path, LAUNCHER_ROOT "/", strlen(LAUNCHER_ROOT) + 1) ||
      !sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  if (launcher_store_init() != ESP_OK)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t e = publish(path, data, size, NULL, NULL);
  xSemaphoreGive(s_lock);
  return e;
}
esp_err_t launcher_store_write(const char *key, const void *data, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!key_valid(key) || !data || size > 65536)
    return ESP_ERR_INVALID_ARG;
  uint8_t *record = malloc(size + 12);
  if (!record)
    return ESP_ERR_NO_MEM;
  uint32_t header[3] = {RECORD_MAGIC, size, crc32(data, size)};
  memcpy(record, header, 12);
  memcpy(record + 12, data, size);
  char path[160];
  snprintf(path, sizeof(path), LAUNCHER_PRIVATE "/%s.dat", key);
  esp_err_t e = launcher_file_write_validated(path, record, size + 12,
                                              record_valid, NULL);
  free(record);
  return e;
}
esp_err_t launcher_web_publish(const char *source, const char *name) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!source || !sd_path_valid(source) || !name ||
      (strcmp(name, "index.html") && strcmp(name, "style.css") &&
       strcmp(name, "app.js") && strcmp(name, "transfers.js")))
    return ESP_ERR_INVALID_ARG;
  FILE *f = fopen(source, "rb");
  if (!f)
    return ESP_ERR_NOT_FOUND;
  uint8_t *data = malloc(131073);
  if (!data) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t n = fread(data, 1, 131073, f);
  bool ok = n > 0 && n <= 131072 && !ferror(f);
  fclose(f);
  char dest[160];
  snprintf(dest, sizeof(dest), LAUNCHER_WEB "/%s", name);
  esp_err_t e = ok ? launcher_file_write(dest, data, n) : ESP_ERR_INVALID_SIZE;
  free(data);
  return e;
}
