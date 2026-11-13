#include "flash_snapshot.h"
#include "boot_request.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_timer.h"
#include "flash_write_guard.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "launcher_store.h"
#include "layout_model.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "sd_files.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define OP_MAGIC 0x31504f46u
#define OP_KEY "flash-operation"
typedef struct {
  uint32_t from, to, length;
  uint8_t sha[32];
} move_t;
typedef struct {
  uint32_t schema, active, kind, count;
  flash_snapshot_t source;
  uint8_t before[4096], after[4096];
  move_t moves[16];
} operation_t;
static esp_err_t read_operation(operation_t *);
static uint32_t le32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static bool md5(const uint8_t *b, size_t n, uint8_t out[16]) {
  return !mbedtls_md5(b, n, out);
}
static bool table_valid(const uint8_t *b) {
  unsigned count = 0;
  for (unsigned at = 0; at < 3072; at += 32) {
    const uint8_t *e = b + at;
    if (e[0] == 0xeb && e[1] == 0xeb) {
      uint8_t sum[16];
      return count && md5(b, at, sum) && !memcmp(sum, e + 16, 16);
    }
    if (e[0] != 0xaa || e[1] != 0x50)
      return false;
    uint32_t a = le32(e + 4), n = le32(e + 8);
    if (!n || a < 0x9000 || a >= FLASH_IMAGE_SIZE || n > FLASH_IMAGE_SIZE - a ||
        (a & 4095) || (n & 4095) || (a < 0x16000 && a + n > 0x13000) ||
        (e[2] == 0 && (a & 65535)))
      return false;
    for (unsigned j = 0; j < count; j++) {
      uint32_t q = le32(b + j * 32 + 4), m = le32(b + j * 32 + 8);
      if (a < q + m && q < a + n)
        return false;
    }
    count++;
  }
  return false;
}
static bool ota_entry(const uint8_t *p) {
  return p[2] == 0 && p[3] >= 16 && p[3] < 32;
}
static bool factory_entry(const uint8_t *p) {
  return p[2] == 0 && p[3] == 0 && le32(p + 4) == 0x20000;
}
static bool protected_equal(const uint8_t *a, const uint8_t *b) {
  if (!table_valid(a) || !table_valid(b))
    return false;
  for (unsigned pass = 0; pass < 2; pass++) {
    const uint8_t *from = pass ? b : a, *to = pass ? a : b;
    bool factory = false, req = false;
    for (unsigned i = 0; i < 3072 && from[i] == 0xaa; i += 32) {
      const uint8_t *e = from + i;
      uint32_t address = le32(e + 4), size = le32(e + 8);
      if (ota_entry(e)) {
        if (address < 0x20000 + layout_factory_size(from) ||
            address + size > layout_app_end(from))
          return false;
        continue;
      }
      bool found = false;
      for (unsigned j = 0; j < 3072 && to[j] == 0xaa; j += 32) {
        if (factory_entry(e) && factory_entry(to + j)) {
          uint8_t copy[32];
          memcpy(copy, to + j, 32);
          memcpy(copy + 8, e + 8, 4);
          if (!memcmp(e, copy, 32))
            found = true;
        } else if (!memcmp(e, to + j, 32))
          found = true;
      }
      if (!found)
        return false;
      if (factory_entry(e)) {
        if ((size & 65535) || !size || size > layout_app_end(from) - 0x30000)
          return false;
        factory = true;
      }
      if (address == 0x12000 && size == 4096)
        req = true;
    }
    if (!factory || !req)
      return false;
  }
  return true;
}
static esp_err_t supported(void) {
  uint32_t size = 0, cap = 0;
  const esp_partition_t *p = esp_ota_get_running_partition();
  if (!p || p->address != 0x20000 || esp_flash_encryption_enabled() ||
      esp_secure_boot_enabled() || esp_flash_get_size(NULL, &size) ||
      size != FLASH_IMAGE_SIZE)
    return ESP_ERR_NOT_SUPPORTED;
  if (esp_flash_read(NULL, &cap, BOOT_REQUEST_OFFSET + LAYOUT_CAP_OFFSET, 4) ||
      cap != LAYOUT_CAP_MAGIC)
    return ESP_ERR_NOT_SUPPORTED;
  return ESP_OK;
}
static esp_err_t hash_region(FILE *f, uint32_t off, uint32_t size,
                             uint8_t out[32], bool flash) {
  uint8_t *buf = malloc(4096);
  if (!buf)
    return ESP_ERR_NO_MEM;
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  esp_err_t e = ESP_OK;
  if (!flash && fseek(f, off, SEEK_SET))
    e = ESP_FAIL;
  for (uint32_t at = 0; !e && at < size;) {
    unsigned n = size - at > 4096 ? 4096 : size - at;
    if (flash ? esp_flash_read(NULL, buf, off + at, n) != ESP_OK
              : fread(buf, 1, n, f) != n) {
      e = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&h, buf, n);
    at += n;
    vTaskDelay(1);
  }
  mbedtls_sha256_finish(&h, out);
  mbedtls_sha256_free(&h);
  free(buf);
  return e;
}
static bool file_span(FILE *f, uint32_t off, uint8_t *b, size_t n) {
  return !fseek(f, off, SEEK_SET) && fread(b, 1, n, f) == n;
}
esp_err_t flash_snapshot_create(flash_snapshot_t *out, partition_progress_fn cb,
                                void *u, char *m, size_t n) {
  if (!out || supported() || flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  mkdir(LAUNCHER_ROOT "/backups", 0755);
  memset(out, 0, sizeof(*out));
  snprintf(out->path, sizeof(out->path),
           LAUNCHER_ROOT "/backups/flash-%lld.full",
           (long long)esp_timer_get_time());
  char temp[528];
  snprintf(temp, sizeof(temp), "%s.part", out->path);
  FILE *f = fopen(temp, "wx");
  if (!f)
    return ESP_FAIL;
  uint8_t *buf = malloc(4096);
  esp_err_t e = buf ? ESP_OK : ESP_ERR_NO_MEM;
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  for (uint32_t off = 0; !e && off < FLASH_IMAGE_SIZE; off += 4096) {
    if (esp_flash_read(NULL, buf, off, 4096) ||
        fwrite(buf, 1, 4096, f) != 4096) {
      e = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&h, buf, 4096);
    if (!(off & 65535) && cb &&
        !cb((uint64_t)off * 90 / FLASH_IMAGE_SIZE, "Backup integral da flash",
            u))
      e = ESP_ERR_INVALID_STATE;
    vTaskDelay(1);
  }
  mbedtls_sha256_finish(&h, out->sha256);
  mbedtls_sha256_free(&h);
  free(buf);
  if (fflush(f) || fsync(fileno(f)))
    e = ESP_FAIL;
  if (fclose(f))
    e = ESP_FAIL;
  uint8_t actual[32];
  if (!e) {
    f = fopen(temp, "rb");
    e = f ? hash_region(f, 0, FLASH_IMAGE_SIZE, actual, false) : ESP_FAIL;
    if (f)
      fclose(f);
    if (!e && memcmp(actual, out->sha256, 32))
      e = ESP_ERR_INVALID_CRC;
  }
  if (!e && rename(temp, out->path))
    e = ESP_FAIL;
  if (e) {
    unlink(temp);
    return e;
  }
  char side[528], body[96];
  snprintf(side, sizeof(side), "%s.sha256", out->path);
  for (unsigned i = 0; i < 32; i++)
    snprintf(body + i * 2, 3, "%02x", out->sha256[i]);
  strcat(body, "\n");
  f = fopen(side, "wx");
  if (!f)
    return ESP_FAIL;
  bool ok = fwrite(body, 1, 65, f) == 65;
  if (fflush(f) || fsync(fileno(f)))
    ok = false;
  if (fclose(f))
    ok = false;
  snprintf(m, n, "Backup integral verificado: %s", out->path);
  return ok ? ESP_OK : ESP_FAIL;
}
esp_err_t flash_snapshot_validate(const char *path, flash_snapshot_t *out,
                                  char *m, size_t n) {
  if (!path || !out || !sd_path_valid(path) || supported())
    return ESP_ERR_INVALID_ARG;
  struct stat st;
  if (stat(path, &st) || st.st_size != FLASH_IMAGE_SIZE)
    return ESP_ERR_INVALID_SIZE;
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_FAIL;
  uint8_t *b = malloc(8192);
  if (!b) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  esp_err_t e = esp_flash_read(NULL, b, 0x8000, 4096);
  if (!e && !table_valid(b)) {
    uint32_t guard = 0;
    esp_err_t guard_error = esp_flash_read(NULL, &guard, FLASH_GUARD_OFFSET, 4);
    if (guard_error)
      return guard_error;
    operation_t *pending = malloc(sizeof(*pending));
    if (guard == OP_MAGIC && pending && read_operation(pending) == ESP_OK &&
        pending->schema == 1 &&
        protected_equal(pending->before, pending->after))
      memcpy(b, pending->before, 4096);
    free(pending);
  }
  if (!e &&
      (!file_span(f, 0x8000, b + 4096, 4096) || !protected_equal(b, b + 4096)))
    e = ESP_ERR_NOT_SUPPORTED;
  /* Self-hosted restore never erases the running launcher/bootloader. A
   * compatible full snapshot must contain identical bytes there, so
   * preservation is exact. */
  uint8_t from[32], current[32];
  uint32_t root_size = layout_factory_size(b);
  uint32_t snapshot_root_size = layout_factory_size(b + 4096);
  if (snapshot_root_size < root_size)
    root_size = snapshot_root_size;
  if (!e) {
    esp_partition_pos_t pos = {.offset = 0x20000,
                               .size = layout_factory_size(b)};
    esp_image_metadata_t meta;
    if (esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta) ||
        meta.image_len > root_size)
      e = ESP_ERR_NOT_SUPPORTED;
  }
  const uint32_t regions[][2] = {{0, 0x8000}, {0x20000, root_size}};
  for (unsigned i = 0; !e && i < 2; i++) {
    e = hash_region(f, regions[i][0], regions[i][1], from, false);
    if (!e)
      e = hash_region(NULL, regions[i][0], regions[i][1], current, true);
    if (!e && memcmp(from, current, 32))
      e = ESP_ERR_NOT_SUPPORTED;
  }
  if (!e)
    e = hash_region(f, 0, FLASH_IMAGE_SIZE, out->sha256, false);
  free(b);
  fclose(f);
  if (!e) {
    char side[528], hex[66], expected[65];
    snprintf(side, sizeof(side), "%s.sha256", path);
    f = fopen(side, "rb");
    if (!f)
      e = ESP_ERR_NOT_FOUND;
    else {
      bool ok = fread(expected, 1, 64, f) == 64;
      fclose(f);
      for (unsigned i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", out->sha256[i]);
      if (!ok || memcmp(expected, hex, 64))
        e = ESP_ERR_INVALID_CRC;
    }
  }
  if (!e)
    snprintf(out->path, sizeof(out->path), "%s", path);
  snprintf(
      m, n, "%s",
      e == ESP_OK ? "Backup integral compatível e íntegro"
      : e == ESP_ERR_NOT_SUPPORTED
          ? "Bootloader/launcher ou infraestrutura diferentes: restaure por USB"
          : "Backup ausente, incompleto ou SHA-256 inválido");
  return e;
}
static void operation_digest(const operation_t *op, uint8_t hash[32]) {
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  const size_t offset = offsetof(operation_t, active);
  const uint32_t active = 1;
  mbedtls_sha256_update(&h, (const uint8_t *)op, offset);
  mbedtls_sha256_update(&h, (const uint8_t *)&active, sizeof(active));
  mbedtls_sha256_update(&h, (const uint8_t *)op + offset + sizeof(active),
                        sizeof(*op) - offset - sizeof(active));
  mbedtls_sha256_finish(&h, hash);
  mbedtls_sha256_free(&h);
}
static void operation_path(const uint8_t hash[32], char path[192]) {
  char hex[65];
  for (unsigned i = 0; i < 32; i++)
    snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  snprintf(path, 192, LAUNCHER_PRIVATE "/flash-%s.op", hex);
}
static esp_err_t durable_operation(const operation_t *op) {
  uint8_t expected[32], actual[32];
  operation_digest(op, expected);
  char path[192];
  operation_path(expected, path);
  esp_err_t e = launcher_file_write(path, op, sizeof(*op));
  FILE *f = NULL;
  operation_t *verify = malloc(sizeof(*verify));
  if (!verify)
    return ESP_ERR_NO_MEM;
  if (!e) {
    f = fopen(path, "rb");
    if (!f || fread(verify, 1, sizeof(*verify), f) != sizeof(*verify) ||
        fgetc(f) != EOF)
      e = ESP_FAIL;
    if (f)
      fclose(f);
  }
  if (!e) {
    operation_digest(verify, actual);
    if (memcmp(actual, expected, 32))
      e = ESP_ERR_INVALID_CRC;
  }
  free(verify);
  return e;
}
static esp_err_t read_operation(operation_t *op) {
  size_t size = sizeof(*op);
  esp_err_t e = launcher_store_read(OP_KEY, op, &size);
  if (!e && size != sizeof(*op))
    e = ESP_ERR_INVALID_SIZE;
  uint32_t guard = 0;
  uint8_t expected[32], actual[32];
  esp_err_t guard_error = esp_flash_read(NULL, &guard, FLASH_GUARD_OFFSET, 4);
  if (guard_error)
    return guard_error;
  if (guard == OP_MAGIC) {
    if (esp_flash_read(NULL, expected, FLASH_GUARD_OFFSET + 4, 32))
      return ESP_FAIL;
    if (!e) {
      operation_digest(op, actual);
      if (memcmp(actual, expected, 32))
        e = ESP_ERR_INVALID_CRC;
    }
    if (e) {
      char path[192];
      operation_path(expected, path);
      FILE *f = fopen(path, "rb");
      if (!f)
        return e;
      bool ok = fread(op, 1, sizeof(*op), f) == sizeof(*op) &&
                fgetc(f) == EOF && !ferror(f);
      fclose(f);
      if (!ok)
        return ESP_ERR_INVALID_CRC;
      operation_digest(op, actual);
      if (memcmp(actual, expected, 32))
        return ESP_ERR_INVALID_CRC;
      e = ESP_OK;
    }
  }
  return e;
}
static esp_err_t guard_write(const operation_t *op) {
  uint8_t sha[32];
  operation_digest(op, sha);
  esp_err_t e = esp_flash_erase_region(NULL, FLASH_GUARD_OFFSET, 4096);
  if (!e)
    e = esp_flash_write(NULL, sha, FLASH_GUARD_OFFSET + 4, 32);
  uint32_t magic = OP_MAGIC;
  if (!e)
    e = esp_flash_write(NULL, &magic, FLASH_GUARD_OFFSET, 4);
  uint32_t check = 0;
  if (!e)
    e = esp_flash_read(NULL, &check, FLASH_GUARD_OFFSET, 4);
  return !e && check != OP_MAGIC ? ESP_FAIL : e;
}
esp_err_t flash_operation_status(bool *pending) {
  if (!pending)
    return ESP_ERR_INVALID_ARG;
  *pending = true; /* Fail closed until both stores have been checked. */
  uint32_t magic = 0;
  esp_err_t e = esp_flash_read(NULL, &magic, FLASH_GUARD_OFFSET, 4);
  if (e)
    return e;
  if (magic == OP_MAGIC)
    return ESP_OK;
  if (magic != 0 && magic != UINT32_MAX)
    return ESP_ERR_INVALID_CRC;
  operation_t *op = malloc(sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  size_t size = sizeof(*op);
  e = launcher_store_read(OP_KEY, op, &size);
  if (e == ESP_ERR_NOT_FOUND) {
    *pending = false;
    e = ESP_OK;
  } else if (!e) {
    if (size != sizeof(*op) || op->schema != 1 || op->active > 1)
      e = ESP_ERR_INVALID_CRC;
    else
      *pending = op->active != 0;
  }
  free(op);
  return e;
}
bool flash_operation_pending(void) {
  bool pending = true;
  return flash_operation_status(&pending) != ESP_OK || pending;
}
static esp_err_t table_commit(operation_t *op) {
  layout_recovery_t tx = {.magic = LAYOUT_TX_MAGIC,
                          .old_crc = boot_crc_bytes(op->before, 4096),
                          .new_crc = boot_crc_bytes(op->after, 4096)};
  tx.crc = boot_crc_bytes(&tx.old_crc, 8);
  esp_err_t e = esp_flash_erase_region(NULL, LAYOUT_MIRROR_OFFSET, 4096);
  if (!e)
    e = esp_flash_write(NULL, op->before, LAYOUT_MIRROR_OFFSET, 4096);
  uint8_t *verify = malloc(4096);
  if (!verify)
    return ESP_ERR_NO_MEM;
  if (!e)
    e = esp_flash_read(NULL, verify, LAYOUT_MIRROR_OFFSET, 4096);
  if (!e && memcmp(verify, op->before, 4096))
    e = ESP_ERR_INVALID_CRC;
  if (!e)
    e = esp_flash_erase_region(NULL, LAYOUT_TX_OFFSET, 4096);
  if (!e)
    e = esp_flash_write(NULL, (uint8_t *)&tx + 4, LAYOUT_TX_OFFSET + 4,
                        sizeof(tx) - 4);
  if (!e)
    e = esp_flash_write(NULL, &tx.magic, LAYOUT_TX_OFFSET, 4);
  if (!e)
    e = esp_flash_read(NULL, verify, LAYOUT_TX_OFFSET, sizeof(tx));
  if (!e && memcmp(verify, &tx, sizeof(tx)))
    e = ESP_ERR_INVALID_CRC;
  if (!e)
    e = layout_table_publish(op->after);
  if (!e)
    e = esp_flash_read(NULL, verify, 0x8000, 4096);
  if (!e && memcmp(verify, op->after, 4096))
    e = ESP_ERR_INVALID_CRC;
  free(verify);
  if (!e) {
    uint32_t zero = 0;
    e = esp_flash_write(NULL, &zero, LAYOUT_TX_OFFSET, 4);
  }
  return e;
}
static esp_err_t copy_span(FILE *f, uint32_t source, uint32_t target,
                           uint32_t length, uint8_t *buf,
                           uint32_t protected_end) {
  esp_err_t e = ESP_OK;
  for (uint32_t off = 0; off < length; off += 4096) {
    unsigned n = length - off > 4096 ? 4096 : length - off;
    if (!file_span(f, source + off, buf, n))
      return ESP_FAIL;
    if ((e = target + off >= protected_end && target + off < 0xff0000
                 ? layout_app_erase(target + off, 4096, protected_end)
                 : esp_flash_erase_region(NULL, target + off, 4096)))
      return e;
    if ((e = target + off >= protected_end && target + off < 0xff0000
                 ? layout_app_write(target + off, buf, n, protected_end)
                 : esp_flash_write(NULL, buf, target + off, n)))
      return e;
    uint8_t expected[32], actual[32];
    mbedtls_sha256(buf, n, expected, 0);
    if ((e = esp_flash_read(NULL, buf, target + off, n)))
      return e;
    mbedtls_sha256(buf, n, actual, 0);
    if (memcmp(expected, actual, 32))
      return ESP_ERR_INVALID_CRC;
    if (!((target + off) & 65535))
      vTaskDelay(1);
  }
  return e;
}
/* A data creation transaction may add exactly one unencrypted user data row.
 * Every existing entry must be byte-identical. */
static const uint8_t *added_data(const operation_t *op) {
  if (!table_valid(op->before) || !table_valid(op->after)) return NULL;
  unsigned old_count = 0, new_count = 0;
  const uint8_t *added = NULL;
  for (unsigned at = 0; at < 3072 && op->before[at] == 0xaa; at += 32) {
    bool found = false;
    old_count++;
    for (unsigned j = 0; j < 3072 && op->after[j] == 0xaa; j += 32)
      if (!memcmp(op->before + at, op->after + j, 32)) found = true;
    if (!found) return NULL;
  }
  for (unsigned at = 0; at < 3072 && op->after[at] == 0xaa; at += 32) {
    const uint8_t *p = op->after + at;
    bool found = false;
    new_count++;
    for (unsigned j = 0; j < 3072 && op->before[j] == 0xaa; j += 32)
      if (!memcmp(p, op->before + j, 32)) found = true;
    if (!found) { if (added) return NULL; added = p; }
  }
  if (new_count != old_count + 1 || !added || added[2] != 1 ||
      (added[3] != 2 && added[3] != 0x81 && added[3] != 0x82 && added[3] != 0x83) ||
      le32(added + 28) || !added[12] || !memchr(added + 12, 0, 16)) return NULL;
  uint32_t a = le32(added + 4), n = le32(added + 8);
  if (!((a >= 0x20000 && a + n <= 0xff0000) ||
        (a == 0x9000 && n == 0x6000 && added[3] == 2))) return NULL;
  return added;
}
/* Compact, disposable migration source: table plus occupied app bytes only.
 * It is not a full-flash backup and remains available for interrupted moves. */
static uint32_t move_offset(const operation_t *op, unsigned index) {
  uint32_t off = 4096;
  for (unsigned i = 0; i < index; i++) off += op->moves[i].length;
  return off;
}
static esp_err_t migration_source(operation_t *op, partition_progress_fn cb,
                                  void *u, char *m, size_t n) {
  if (!op->count) { op->kind = 4; return ESP_OK; }
  mkdir(LAUNCHER_PRIVATE, 0755);
  snprintf(op->source.path, sizeof(op->source.path),
           LAUNCHER_PRIVATE "/migration-%lld.work", (long long)esp_timer_get_time());
  FILE *f = fopen(op->source.path, "wx");
  if (!f) return ESP_FAIL;
  uint8_t *buf = malloc(4096);
  esp_err_t e = buf && fwrite(op->before, 1, 4096, f) == 4096 ? ESP_OK : ESP_FAIL;
  for (unsigned i = 0; !e && i < op->count; i++) {
    for (uint32_t off = 0; !e && off < op->moves[i].length; off += 4096) {
      e = esp_flash_read(NULL, buf, op->moves[i].from + off, 4096);
      if (!e && fwrite(buf, 1, 4096, f) != 4096) e = ESP_FAIL;
      if (!e && cb && !cb(5, "Preparando copia temporaria dos apps", u))
        e = ESP_ERR_INVALID_STATE;
      vTaskDelay(1);
    }
  }
  free(buf);
  if (fflush(f) || fsync(fileno(f))) e = ESP_FAIL;
  if (fclose(f)) e = ESP_FAIL;
  if (!e) {
    f = fopen(op->source.path, "rb");
    e = f ? hash_region(f, 0, move_offset(op, op->count), op->source.sha256, false) : ESP_FAIL;
    if (f) fclose(f);
  }
  if (e) unlink(op->source.path);
  else op->kind = 6;
  return e;
}
static esp_err_t run_operation(operation_t *op, partition_progress_fn cb,
                               void *u, char *m, size_t n) {
  if (op->kind == 4 || op->kind == 5) {
    const uint8_t *data = op->kind == 5 ? added_data(op) : NULL;
    if (op->schema != 1 || op->count ||
        (op->kind == 5 ? !data : !protected_equal(op->before, op->after)))
      return ESP_ERR_INVALID_ARG;
    uint8_t *current = malloc(4096);
    if (!current) return ESP_ERR_NO_MEM;
    esp_err_t e = esp_flash_read(NULL, current, 0x8000, 4096);
    if (!e && memcmp(current, op->before, 4096) &&
        memcmp(current, op->after, 4096)) e = ESP_ERR_INVALID_STATE;
    free(current);
    if (!e && cb && !cb(0, "Atualizando mapa vazio", u))
      e = ESP_ERR_INVALID_STATE;
    op->active = 1;
    if (!e) e = launcher_store_write(OP_KEY, op, sizeof(*op));
    if (!e) e = durable_operation(op);
    if (!e) e = guard_write(op);
    if (!e && data) {
      for (uint32_t off = 0; !e && off < le32(data + 8); off += 4096) {
        e = esp_flash_erase_region(NULL, le32(data + 4) + off, 4096);
        vTaskDelay(1);
      }
    }
    if (!e) e = table_commit(op);
    if (!e) {
      op->active = 0;
      e = launcher_store_write(OP_KEY, op, sizeof(*op));
      uint32_t zero = 0;
      if (!e) e = esp_flash_write(NULL, &zero, FLASH_GUARD_OFFSET, 4);
    }
    snprintf(m, n, "%s", e ? "Mapa interrompido; use Retomar em FLASH."
                            : "Mapa atualizado e verificado.");
    return e;
  }
  if ((op->kind != 1 && op->kind != 2 && op->kind != 3 && op->kind != 6) || op->schema != 1 ||
      op->count > 16 || !protected_equal(op->before, op->after) ||
      !memchr(op->source.path, 0, sizeof(op->source.path)))
    return ESP_ERR_INVALID_ARG;
  flash_snapshot_t checked;
  esp_err_t e;
  if (op->kind == 6) {
    FILE *source = fopen(op->source.path, "rb");
    struct stat st;
    e = source && !fstat(fileno(source), &st) &&
        st.st_size == move_offset(op, op->count)
        ? hash_region(source, 0, st.st_size, checked.sha256, false) : ESP_FAIL;
    if (source) fclose(source);
  } else e = flash_snapshot_validate(op->source.path, &checked, m, n);
  if (e || memcmp(checked.sha256, op->source.sha256, 32))
    return e ? e : ESP_ERR_INVALID_CRC;
  uint8_t *buf = malloc(4096);
  if (!buf)
    return ESP_ERR_NO_MEM;
  FILE *f = fopen(op->source.path, "rb");
  if (!f) {
    free(buf);
    return ESP_FAIL;
  }
  uint8_t *table = malloc(4096);
  if (!table) {
    fclose(f);
    free(buf);
    return ESP_ERR_NO_MEM;
  }
  if (!file_span(f, op->kind == 6 ? 0 : 0x8000, table, 4096) ||
      memcmp(table, (op->kind == 1 || op->kind == 3 || op->kind == 6) ? op->before : op->after,
             4096))
    e = ESP_ERR_INVALID_CRC;
  if (!e && esp_flash_read(NULL, table, 0x8000, 4096))
    e = ESP_FAIL;
  if (!e && table_valid(table) && memcmp(table, op->before, 4096) &&
      memcmp(table, op->after, 4096))
    e = ESP_ERR_INVALID_STATE;
  free(table);
  for (unsigned i = 0; !e && i < op->count; i++) {
    move_t *x = &op->moves[i];
    bool source = false, target = false;
    if (x->from < 0x20000 + layout_factory_size(op->before) ||
        x->from >= layout_app_end(op->before) ||
        x->to < 0x20000 + layout_factory_size(op->after) || x->to >= layout_app_end(op->after) ||
        !x->length || (x->length & 4095) || x->length > layout_app_end(op->before) - x->from ||
        x->length > layout_app_end(op->after) - x->to) {
      e = ESP_ERR_INVALID_ARG;
      break;
    }
    for (unsigned at = 0; at < 3072 && op->before[at] == 0xaa; at += 32)
      if (op->before[at + 2] == 0 && le32(op->before + at + 4) == x->from &&
          x->length <= le32(op->before + at + 8))
        source = true;
    for (unsigned at = 0; at < 3072 && op->after[at] == 0xaa; at += 32)
      if (op->after[at + 2] == 0 && le32(op->after + at + 4) == x->to &&
          x->length <= le32(op->after + at + 8))
        target = true;
    if (!source || !target) {
      e = ESP_ERR_INVALID_ARG;
      break;
    }
    for (unsigned j = 0; j < i; j++)
      if (x->to < op->moves[j].to + op->moves[j].length &&
          op->moves[j].to < x->to + x->length)
        e = ESP_ERR_INVALID_ARG;
    uint8_t digest[32];
    if (!e)
      e = hash_region(f, op->kind == 6 ? move_offset(op, i) : x->from, x->length, digest, false);
    if (!e && memcmp(digest, x->sha, 32))
      e = ESP_ERR_INVALID_CRC;
  }
  if (e) {
    fclose(f);
    free(buf);
    return e;
  }
  if (cb && !cb(0, "Último ponto de cancelamento", u)) {
    fclose(f);
    free(buf);
    return ESP_ERR_INVALID_STATE;
  }
  op->active = 1;
  e = launcher_store_write(OP_KEY, op, sizeof(*op));
  if (!e)
    e = durable_operation(op);
  if (!e)
    e = guard_write(op);
  if (!e && (op->kind == 1 || op->kind == 3 || op->kind == 6)) {
    for (uint32_t off = 0x20000 + layout_factory_size(op->after);
         !e && off < layout_app_end(op->after); off += 65536) {
      e = layout_app_erase(off, 65536,
                           0x20000 + layout_factory_size(op->after));
      if (cb)
        cb(10, "Preparando banco de apps; não interrompa", u);
      vTaskDelay(1);
    }
    for (unsigned i = 0; !e && i < op->count; i++) {
      move_t *x = &op->moves[i];
      if (x->from < 0x20000 + layout_factory_size(op->before) ||
          x->to < 0x20000 + layout_factory_size(op->after) || !x->length ||
          (x->length & 4095) || x->length > layout_app_end(op->before) - x->from ||
          x->length > layout_app_end(op->after) - x->to) {
        e = ESP_ERR_INVALID_ARG;
        break;
      }
      e = copy_span(f, op->kind == 6 ? move_offset(op, i) : x->from, x->to, x->length, buf,
                    0x20000 + layout_factory_size(op->after));
      uint8_t actual[32];
      if (!e)
        e = hash_region(NULL, x->to, x->length, actual, true);
      if (!e && memcmp(actual, x->sha, 32))
        e = ESP_ERR_INVALID_CRC;
      if (cb)
        cb(20 + (i + 1) * 65 / (op->count ? op->count : 1),
           "Aplicativo migrado e verificado", u);
    }
  } else if (!e && op->kind == 2) {
    for (uint32_t off = 0x9000; !e && off < FLASH_IMAGE_SIZE; off += 4096) {
      if ((off >= 0x12000 && off < 0x16000) ||
          (off >= 0x20000 &&
           off < 0x20000 + (layout_factory_size(op->before) <
                                    layout_factory_size(op->after)
                                ? layout_factory_size(op->before)
                                : layout_factory_size(op->after))))
        continue;
      e = copy_span(f, off, off, 4096, buf,
                    0x20000 + (layout_factory_size(op->before) <
                                       layout_factory_size(op->after)
                                   ? layout_factory_size(op->before)
                                   : layout_factory_size(op->after)));
      if (!(off & 65535) && cb)
        cb((uint64_t)off * 90 / FLASH_IMAGE_SIZE,
           "Restaurando flash; não interrompa", u);
    }
  } else if (!e)
    e = ESP_ERR_INVALID_ARG;
  if (!e)
    e = table_commit(op);
  fclose(f);
  free(buf);
  if (!e) {
    op->active = 0;
    e = launcher_store_write(OP_KEY, op, sizeof(*op));
    if (!e) {
      uint32_t zero = 0;
      e = esp_flash_write(NULL, &zero, FLASH_GUARD_OFFSET, 4);
    }
  }
  if (!e && op->kind == 6) unlink(op->source.path);
  snprintf(m, n, "%s",
           e ? "Operação interrompida. Não inicie apps; use Retomar em FLASH."
             : "Flash verificada. Reinicie o Tab5.");
  return e;
}
static esp_err_t migration_model(const uint32_t *s, unsigned count,
                                 operation_t *op, char *m, size_t n) {
  esp_err_t e = partitions_mgr_profile_plan(s, count, op->source.sha256);
  if (e)
    return e;
  e = esp_flash_read(NULL, op->before, 0x8000, 4096);
  if (e || !layout_build_profile(op->before, op->after, s, count, md5) ||
      !protected_equal(op->before, op->after))
    return ESP_ERR_NOT_SUPPORTED;
  uint32_t used[8] = {0}, from[8] = {0}, to[4] = {0};
  unsigned old_count = 0;
  for (unsigned at = 0; at < 3072 && op->before[at] == 0xaa; at += 32) {
    uint8_t *x = op->before + at;
    uint32_t address = le32(x + 4), size = le32(x + 8);
    if (x[2] || x[3] < 16 || address < 0x210000 || address >= 0xa10000)
      continue;
    unsigned id = x[3] - 16;
    if (id >= 8)
      return ESP_ERR_NOT_SUPPORTED;
    if (old_count <= id)
      old_count = id + 1;
    from[id] = address;
    uint8_t *buf = malloc(4096);
    if (!buf)
      return ESP_ERR_NO_MEM;
    for (uint32_t end = size; end; end -= 4096) {
      if (!(end & 65535)) vTaskDelay(1);
      e = esp_flash_read(NULL, buf, address + end - 4096, 4096);
      if (e)
        break;
      bool empty = true;
      for (unsigned j = 0; j < 4096; j++)
        if (buf[j] != 255) {
          empty = false;
          break;
        }
      if (!empty) {
        used[id] = end;
        break;
      }
    }
    free(buf);
    if (e)
      return e;
    if (used[id]) {
      esp_partition_pos_t pos = {.offset = address, .size = size};
      esp_image_metadata_t meta;
      if (esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta))
        return ESP_ERR_INVALID_CRC;
    }
  }
  int map[8];
  if (!layout_assign_apps(used, old_count, s, count, map)) {
    snprintf(m, n,
             "Os apps instalados não cabem nos slots escolhidos. Aumente os "
             "tamanhos.");
    return ESP_ERR_INVALID_SIZE;
  }
  uint32_t address = 0x210000;
  for (unsigned i = 0; i < count; i++) {
    to[i] = address;
    address += s[i];
  }
  op->schema = 1;
  op->kind = 1;
  op->count = 0;
  for (unsigned i = 0; i < old_count; i++)
    if (used[i]) {
      move_t *x = &op->moves[op->count++];
      x->from = from[i];
      x->to = to[map[i]];
      x->length = used[i];
      e = hash_region(NULL, x->from, x->length, x->sha, true);
      if (e)
        return e;
    }
  snprintf(m, n,
           "%u aplicativo(s) serão preservados e verificados. Backup integral "
           "antes da migração.",
           (unsigned)op->count);
  return ESP_OK;
}
esp_err_t flash_migrate_plan(const uint32_t *s, unsigned count, uint8_t sha[32],
                             char *m, size_t n) {
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = migration_model(s, count, op, m, n);
  if (!e)
    operation_digest(op, sha);
  free(op);
  return e;
}
esp_err_t flash_migrate_apply(const uint32_t *s, unsigned count,
                              const uint8_t expected[32],
                              partition_progress_fn cb, void *u, char *m,
                              size_t n) {
  if (flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = migration_model(s, count, op, m, n);
  uint8_t actual[32];
  if (!e) {
    operation_digest(op, actual);
    if (memcmp(actual, expected, 32))
      e = ESP_ERR_INVALID_STATE;
  }
  if (!e)
    e = flash_snapshot_create(&op->source, cb, u, m, n);
  if (!e)
    e = run_operation(op, cb, u, m, n);
  free(op);
  return e;
}
esp_err_t flash_snapshot_restore_options(const flash_snapshot_t *s, bool backup,
                                 partition_progress_fn cb, void *u, char *m,
                                 size_t n) {
  if (!s || flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = flash_snapshot_validate(s->path, &op->source, m, n);
  if (!e && memcmp(s->sha256, op->source.sha256, 32))
    e = ESP_ERR_INVALID_CRC;
  if (!e) {
    op->schema = 1;
    op->kind = 2;
    e = esp_flash_read(NULL, op->before, 0x8000, 4096);
    FILE *f = fopen(s->path, "rb");
    if (!f)
      e = ESP_FAIL;
    else {
      if (!file_span(f, 0x8000, op->after, 4096))
        e = ESP_FAIL;
      fclose(f);
    }
  }
  if (!e && backup) {
    flash_snapshot_t previous;
    e = flash_snapshot_create(&previous, cb, u, m, n);
  }
  if (!e)
    e = run_operation(op, cb, u, m, n);
  free(op);
  return e;
}
esp_err_t flash_operation_resume(partition_progress_fn cb, void *u, char *m,
                                 size_t n) {
  operation_t *op = malloc(sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = read_operation(op);
  if (!e)
    e = run_operation(op, cb, u, m, n);
  else
    e = ESP_ERR_INVALID_STATE;
  free(op);
  return e;
}
static esp_err_t allocation_model(uint32_t required, const uint32_t *requested,
                                  operation_t *op, flash_allocation_t *out,
                                  char *m, size_t n) {
  if (supported() || flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  if (required > 0xff0000 - 0x30000) {
    snprintf(m, n,
             "Imagem maior que o banco de aplicativos disponivel nesta flash.");
    return ESP_ERR_INVALID_SIZE;
  }
  esp_err_t e = esp_flash_read(NULL, op->before, 0x8000, 4096);
  if (e || !table_valid(op->before))
    return ESP_ERR_INVALID_CRC;
  uint32_t old_factory = layout_factory_size(op->before);
  if (!old_factory)
    return ESP_ERR_NOT_SUPPORTED;
  esp_partition_pos_t root = {.offset = 0x20000, .size = old_factory};
  esp_image_metadata_t image;
  e = esp_image_verify(ESP_IMAGE_VERIFY, &root, &image);
  if (e)
    return e;
  uint32_t factory = (image.image_len + 65535u) & ~65535u;
  uint32_t sizes[16] = {0}, used[16] = {0}, from[16] = {0};
  unsigned occupied = 0;
  uint64_t sum = 0;
  uint8_t *buf = malloc(4096);
  if (!buf)
    return ESP_ERR_NO_MEM;
  for (unsigned at = 0; !e && at < 3072 && op->before[at] == 0xaa; at += 32) {
    const uint8_t *p = op->before + at;
    if (!ota_entry(p))
      continue;
    unsigned slot = p[3] - 16;
    uint32_t address = le32(p + 4), size = le32(p + 8);
    if (address < 0x20000 + old_factory || address + size > layout_app_end(op->before)) {
      e = ESP_ERR_NOT_SUPPORTED;
      break;
    }
    from[slot] = address;
    out->reserved[slot] = size;
    for (uint32_t end = size; end; end -= 4096) {
      if (!(end & 65535)) vTaskDelay(1);
      e = esp_flash_read(NULL, buf, address + end - 4096, 4096);
      if (e)
        break;
      bool blank = true;
      for (unsigned j = 0; j < 4096; j++)
        if (buf[j] != 255) {
          blank = false;
          break;
        }
      if (!blank) {
        used[slot] = end;
        break;
      }
    }
    if (e)
      break;
    if (used[slot]) {
      esp_partition_pos_t pos = {.offset = address, .size = size};
      e = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &image);
      if (e) {
        snprintf(m, n,
                 "App %u invalido. Remova-o pela HOME antes de compactar.",
                 slot);
        break;
      }
      sizes[slot] = (used[slot] + 65535u) & ~65535u;
      out->minimum[slot] = sizes[slot];
      sum += sizes[slot];
      occupied++;
    }
  }
  free(buf);
  if (e)
    return e;
  uint32_t capacity = layout_app_end(op->before) - 0x20000 - factory;
  if (requested) {
    if (required)
      return ESP_ERR_INVALID_ARG;
    sum = 0;
    for (unsigned i = 0; i < 16; i++) {
      if ((requested[i] & 65535) || requested[i] > capacity) {
        snprintf(
            m, n,
            "Use tamanhos alinhados a 64 KiB, dentro do banco disponivel.");
        return ESP_ERR_INVALID_SIZE;
      }
      if (requested[i] < out->minimum[i]) {
        snprintf(m, n,
                 "App %u precisa de pelo menos %lu KiB. Remova-o pela HOME "
                 "para liberar essa area.",
                 i, (unsigned long)out->minimum[i] / 1024);
        return ESP_ERR_INVALID_SIZE;
      }
      sizes[i] = requested[i];
      sum += sizes[i];
    }
  }

  int slot = -1;
  if (required)
    for (unsigned i = 0; i < 16; i++)
      if (!used[i]) {
        slot = i;
        break;
      }
  uint32_t need = (required + 65535u) & ~65535u;
  if (sum + need > capacity || (required && slot < 0)) {
    if (requested) {
      snprintf(m, n,
               "Reservas de %.2f MiB excedem o banco de %.2f MiB. Reduza as "
               "areas antes de aplicar.",
               (double)sum / 1048576, (double)capacity / 1048576);
      return ESP_ERR_NO_MEM;
    }
    snprintf(m, n,
             "Sem espaco: %.2f MiB livres; app precisa %.2f MiB. Remova apps "
             "na HOME e tente novamente.",
             sum <= capacity ? (double)(capacity - sum) / 1048576 : 0.,
             (double)need / 1048576);
    return ESP_ERR_NO_MEM;
  }
  if (slot >= 0)
    sizes[slot] = need;
  if (!layout_build_compact(op->before, op->after, factory, sizes, md5) ||
      !protected_equal(op->before, op->after))
    return ESP_ERR_NOT_SUPPORTED;
  op->schema = 1;
  op->kind = 3;
  op->count = 0;
  uint32_t address = 0x20000 + factory;
  for (unsigned i = 0; i < 16; i++)
    if (sizes[i]) {
      if ((int)i == slot) {
        out->slot = slot;
        out->address = address;
        out->size = sizes[i];
      }
      if (used[i]) {
        move_t *move = &op->moves[op->count++];
        move->from = from[i];
        move->to = address;
        move->length = used[i];
        e = hash_region(NULL, from[i], used[i], move->sha, true);
        if (e)
          return e;
      }
      address += sizes[i];
    }
  out->capacity = capacity;
  out->launcher_size = factory;
  out->free_bytes = capacity - (uint32_t)sum - need;
  out->changed = memcmp(op->before, op->after, 4096) != 0;
  operation_digest(op, out->digest);
  snprintf(m, n,
           "%s: %u app(s) preservado(s), launcher %.2f MiB; "
           "%.2f MiB livres depois da operacao.",
           requested ? "Mapa manual" : "Alocacao automatica", occupied,
           (double)factory / 1048576, (double)out->free_bytes / 1048576);
  return ESP_OK;
}
esp_err_t flash_allocate_plan(uint32_t required, flash_allocation_t *out,
                              char *m, size_t n) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->slot = -1;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = allocation_model(required, NULL, op, out, m, n);
  free(op);
  return e;
}
esp_err_t flash_allocate_apply_options(uint32_t required, const uint8_t expected[32], bool backup,
                               partition_progress_fn cb, void *u, char *m,
                               size_t n) {
  if (!expected)
    return ESP_ERR_INVALID_ARG;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  flash_allocation_t allocation;
  memset(&allocation, 0, sizeof(allocation));
  esp_err_t e = allocation_model(required, NULL, op, &allocation, m, n);
  if (!e && memcmp(expected, allocation.digest, 32))
    e = ESP_ERR_INVALID_STATE;
  if (!e && !allocation.changed) {
    free(op);
    return ESP_OK;
  }
  if (!e && !op->count) op->kind = 4;
  if (!e && backup) e = flash_snapshot_create(&op->source, cb, u, m, n);
  if (!e && !backup) e = migration_source(op, cb, u, m, n);
  if (!e)
    e = run_operation(op, cb, u, m, n);
  free(op);
  return e;
}

esp_err_t flash_layout_plan(const uint32_t sizes[16], flash_allocation_t *out,
                            char *m, size_t n) {
  if (!sizes || !out)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  out->slot = -1;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  esp_err_t e = allocation_model(0, sizes, op, out, m, n);
  free(op);
  return e;
}
esp_err_t flash_layout_apply_options(const uint32_t sizes[16],
                             const uint8_t expected[32], bool backup,
                             partition_progress_fn cb, void *u, char *m,
                             size_t n) {
  if (!sizes || !expected)
    return ESP_ERR_INVALID_ARG;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op)
    return ESP_ERR_NO_MEM;
  flash_allocation_t allocation = {0};
  esp_err_t e = allocation_model(0, sizes, op, &allocation, m, n);
  if (!e && memcmp(expected, allocation.digest, 32))
    e = ESP_ERR_INVALID_STATE;
  if (!e && allocation.changed) {
    if (!op->count) op->kind = 4;
    if (backup) e = flash_snapshot_create(&op->source, cb, u, m, n);
    else e = migration_source(op, cb, u, m, n);
    if (!e)
      e = run_operation(op, cb, u, m, n);
  }
  free(op);
  return e;
}

esp_err_t flash_allocate_apply(uint32_t required, const uint8_t expected[32],
 partition_progress_fn cb, void *u, char *m, size_t n) {
  return flash_allocate_apply_options(required, expected, false, cb, u, m, n);
}
esp_err_t flash_layout_apply(const uint32_t sizes[16], const uint8_t expected[32],
 partition_progress_fn cb, void *u, char *m, size_t n) {
  return flash_layout_apply_options(sizes, expected, false, cb, u, m, n);
}

static void data_put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++) p[i] = n >> (8 * i);
}
esp_err_t flash_data_create(const char *label, uint8_t subtype, uint32_t size,
 uint32_t address, bool backup, partition_progress_fn cb, void *u, char *m, size_t n) {
  if (!label || !*label || strlen(label) > 15 || !size || (size & 4095) ||
      size > 0xfd0000 || (subtype != 2 && subtype != 0x81 && subtype != 0x82 && subtype != 0x83))
    return ESP_ERR_INVALID_ARG;
  for (const char *p = label; *p; p++)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) return ESP_ERR_INVALID_ARG;
  if (!strcmp(label,"launch_req") || !strcmp(label,"otadata") ||
      !strcmp(label,"phy_init") || !strcmp(label,"coredump") ||
      !strcmp(label,"nvs_keys")) return ESP_ERR_INVALID_ARG;
  if (supported() || flash_operation_pending()) return ESP_ERR_INVALID_STATE;
  operation_t *op = calloc(1, sizeof(*op));
  if (!op) return ESP_ERR_NO_MEM;
  esp_err_t e = esp_flash_read(NULL, op->before, 0x8000, 4096);
  unsigned entries = 0;
  if (!e && !table_valid(op->before)) e = ESP_ERR_INVALID_CRC;
  for (unsigned at = 0; !e && at < 3072 && op->before[at] == 0xaa; at += 32) {
    entries++;
    if (!strncmp((const char *)op->before + at + 12, label, 16)) e = ESP_ERR_INVALID_STATE;
  }
  if (entries >= 94) e = ESP_ERR_NO_MEM;
  bool automatic = address == 0;
  if (automatic) address = subtype == 2 && size == 0x6000 ? 0x9000 : 0xff0000 - size;
  bool available = false;
  while (!e) {
    available = address && !(address & 4095) &&
      ((address >= 0x20000 && address <= 0xff0000 && size <= 0xff0000 - address) ||
       (address == 0x9000 && size == 0x6000 && subtype == 2));
    for (unsigned i = 0; available && i < entries; i++) {
      const uint8_t *row = op->before + i * 32;
      uint32_t a = le32(row + 4), len = le32(row + 8);
      if (address < a + len && a < address + size) available = false;
    }
    if (!available && automatic && address == 0x9000) {
      address = 0xff0000 - size;
      continue;
    }
    if (available || !automatic || address < 0x21000) break;
    address -= 4096;
  }
  if (!e && !available) e = ESP_ERR_NO_MEM;
  if (!e) {
    memset(op->after, 255, 4096);
    memcpy(op->after, op->before, entries * 32);
    uint8_t *row = op->after + entries * 32;
    memset(row, 0, 32); row[0]=0xaa; row[1]=0x50; row[2]=1; row[3]=subtype;
    data_put32(row + 4, address); data_put32(row + 8, size);
    memcpy(row + 12, label, strlen(label));
    row += 32; row[0]=row[1]=0xeb;
    if (!md5(op->after, (entries + 1) * 32, row + 16)) e = ESP_FAIL;
    op->kind = 5; op->schema = 1;
    if (!e && !added_data(op)) e = ESP_ERR_INVALID_ARG;
    if (!e && backup) e = flash_snapshot_create(&op->source, cb, u, m, n);
    if (!e) e = run_operation(op, cb, u, m, n);
  }
  if (!e) snprintf(m,n,"Particao %s criada @0x%lx (%lu KiB). Reinicie.",label,
                    (unsigned long)address,(unsigned long)size/1024);
  else snprintf(m,n,"Nao foi possivel criar a particao: %s",esp_err_to_name(e));
  free(op);
  return e;
}

esp_err_t flash_snapshot_restore(const flash_snapshot_t *s, partition_progress_fn cb,
 void *u, char *m, size_t n) {
  return flash_snapshot_restore_options(s, false, cb, u, m, n);
}
