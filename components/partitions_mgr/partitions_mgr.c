#include "partitions_mgr.h"
#include "boot_request.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_secure_boot.h"
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
static bool md5(const uint8_t *b, size_t n, uint8_t out[16]) {
  return mbedtls_md5(b, n, out) == 0;
}
esp_err_t partitions_mgr_init(void) { return ESP_OK; }
esp_err_t partitions_mgr_big_plan(uint8_t sha[32]) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  uint32_t size = 0;
  if (esp_flash_encryption_enabled() || esp_secure_boot_enabled() || !running ||
      running->address != 0x20000 ||
      esp_flash_get_size(NULL, &size) != ESP_OK || size != 0x1000000)
    return ESP_ERR_NOT_SUPPORTED;
  uint32_t capability = 0;
  if (esp_flash_read(NULL, &capability, BOOT_REQUEST_OFFSET + LAYOUT_CAP_OFFSET,
                     sizeof(capability)) != ESP_OK ||
      capability != LAYOUT_CAP_MAGIC)
    return ESP_ERR_NOT_SUPPORTED;
  uint8_t *b = malloc(8192);
  if (!b)
    return ESP_ERR_NO_MEM;
  esp_err_t err = esp_flash_read(NULL, b, 0x8000, 4096);
  if (err == ESP_OK && !layout_build_big(b, b + 4096, md5))
    err = ESP_ERR_NOT_SUPPORTED;
  if (err == ESP_OK)
    err = mbedtls_sha256(b, 4096, sha, 0) == 0 ? ESP_OK : ESP_FAIL;
  free(b);
  return err;
}
esp_err_t partitions_mgr_profile_plan(const uint32_t *sizes, unsigned count,
                                      uint8_t sha[32]) {
  const esp_partition_t *running = esp_ota_get_running_partition();
  uint32_t size = 0;
  if (esp_flash_encryption_enabled() || esp_secure_boot_enabled() || !running ||
      running->address != 0x20000 ||
      esp_flash_get_size(NULL, &size) != ESP_OK || size != 0x1000000)
    return ESP_ERR_NOT_SUPPORTED;
  uint32_t capability = 0;
  if (esp_flash_read(NULL, &capability, BOOT_REQUEST_OFFSET + LAYOUT_CAP_OFFSET,
                     sizeof(capability)) != ESP_OK ||
      capability != LAYOUT_CAP_MAGIC)
    return ESP_ERR_NOT_SUPPORTED;
  uint8_t *b = malloc(8192);
  if (!b)
    return ESP_ERR_NO_MEM;
  esp_err_t err = esp_flash_read(NULL, b, 0x8000, 4096);
  if (err == ESP_OK && !layout_build_profile(b, b + 4096, sizes, count, md5))
    err = ESP_ERR_NOT_SUPPORTED;
  if (err == ESP_OK && !memcmp(b, b + 4096, 4096))
    err = ESP_ERR_INVALID_STATE;
  if (err == ESP_OK)
    err = mbedtls_sha256(b, 4096, sha, 0) == 0 ? ESP_OK : ESP_FAIL;
  free(b);
  return err;
}
static esp_err_t full_backup(partition_progress_fn cb, void *user,
                             char *message, unsigned size) {
  mkdir(LAUNCHER_ROOT "/backups", 0775);
  const char *temp = LAUNCHER_ROOT "/backups/layout-before.bin.part";
  const char *dest = LAUNCHER_ROOT "/backups/layout-before.bin";
  struct stat st;
  if (stat(dest, &st) == 0 || stat(temp, &st) == 0) {
    snprintf(message, size,
             "Mova o backup layout-before.bin anterior antes de reorganizar.");
    return ESP_ERR_INVALID_STATE;
  }
  FILE *f = fopen(temp, "wb");
  if (!f)
    return ESP_FAIL;
  uint8_t b[4096], expected[32], actual[32];
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  esp_err_t err = ESP_OK;
  for (uint32_t off = 0; off < 0x1000000; off += sizeof(b)) {
    if (esp_flash_read(NULL, b, off, sizeof(b)) != ESP_OK ||
        fwrite(b, 1, sizeof(b), f) != sizeof(b)) {
      err = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&h, b, sizeof(b));
    if ((off & 65535) == 0) {
      if (cb && !cb((uint64_t)off * 90 / 0x1000000,
                    "Backup completo antes do layout", user)) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }
      vTaskDelay(1);
    }
  }
  mbedtls_sha256_finish(&h, expected);
  mbedtls_sha256_free(&h);
  if (fflush(f) || fsync(fileno(f)))
    err = ESP_FAIL;
  if (fclose(f))
    err = ESP_FAIL;
  if (err == ESP_OK) {
    f = fopen(temp, "rb");
    if (!f)
      err = ESP_FAIL;
    else {
      mbedtls_sha256_init(&h);
      mbedtls_sha256_starts(&h, 0);
      uint32_t total = 0;
      size_t n;
      while ((n = fread(b, 1, sizeof(b), f))) {
        mbedtls_sha256_update(&h, b, n);
        total += n;
        vTaskDelay(1);
      }
      if (ferror(f) || total != 0x1000000)
        err = ESP_FAIL;
      fclose(f);
      mbedtls_sha256_finish(&h, actual);
      mbedtls_sha256_free(&h);
      if (memcmp(expected, actual, 32))
        err = ESP_FAIL;
    }
  }
  if (err == ESP_OK && rename(temp, dest))
    err = ESP_FAIL;
  if (err != ESP_OK) {
    unlink(temp);
    return err;
  }
  f = fopen(LAUNCHER_ROOT "/backups/layout-before.sha256", "w");
  if (!f)
    return ESP_FAIL;
  for (int i = 0; i < 32; i++)
    fprintf(f, "%02x", expected[i]);
  fprintf(f, "  layout-before.bin\n");
  bool failed = fflush(f) != 0 || fsync(fileno(f)) != 0;
  if (fclose(f) != 0)
    failed = true;
  return failed ? ESP_FAIL : ESP_OK;
}
static esp_err_t apply_layout(const uint8_t expected[32], const uint32_t *sizes,
                              unsigned count, partition_progress_fn cb,
                              void *user, char *message, unsigned size) {
  uint8_t actual[32];
  esp_err_t err = sizes ? partitions_mgr_profile_plan(sizes, count, actual)
                        : partitions_mgr_big_plan(actual);
  if (err != ESP_OK || memcmp(actual, expected, 32))
    return ESP_ERR_INVALID_STATE;
  if ((err = full_backup(cb, user, message, size)) != ESP_OK)
    return err;
  if (cb && !cb(95, "Preparando tabela; ultimo ponto de cancelamento", user))
    return ESP_ERR_INVALID_STATE;
  uint8_t *b = malloc(8192);
  if (!b)
    return ESP_ERR_NO_MEM;
  err = esp_flash_read(NULL, b, 0x8000, 4096);
  if (err == ESP_OK &&
      (!(sizes ? layout_build_profile(b, b + 4096, sizes, count, md5)
               : layout_build_big(b, b + 4096, md5)) ||
       mbedtls_sha256(b, 4096, actual, 0) != 0 || memcmp(actual, expected, 32)))
    err = ESP_ERR_INVALID_STATE;
  if (err == ESP_OK) {
    layout_recovery_t tx = {.magic = LAYOUT_TX_MAGIC,
                            .old_crc = boot_crc_bytes(b, 4096),
                            .new_crc = boot_crc_bytes(b + 4096, 4096)};
    tx.crc = boot_crc_bytes(&tx.old_crc, 8);
    err = esp_flash_erase_region(NULL, LAYOUT_MIRROR_OFFSET, 4096);
    if (err == ESP_OK)
      err = esp_flash_write(NULL, b, LAYOUT_MIRROR_OFFSET, 4096);
    if (err == ESP_OK) {
      uint8_t mirror[4096];
      err = esp_flash_read(NULL, mirror, LAYOUT_MIRROR_OFFSET, 4096);
      if (err == ESP_OK && memcmp(mirror, b, 4096))
        err = ESP_FAIL;
    }
    if (err == ESP_OK)
      err = esp_flash_erase_region(NULL, LAYOUT_TX_OFFSET, 4096);
    if (err == ESP_OK)
      err = esp_flash_write(NULL, (uint8_t *)&tx + 4, LAYOUT_TX_OFFSET + 4,
                            sizeof(tx) - 4);
    if (err == ESP_OK)
      err = esp_flash_write(NULL, &tx.magic, LAYOUT_TX_OFFSET, 4);
    if (err == ESP_OK) {
      layout_recovery_t check;
      err = esp_flash_read(NULL, &check, LAYOUT_TX_OFFSET, sizeof(check));
      if (err == ESP_OK && memcmp(&check, &tx, sizeof(tx)))
        err = ESP_FAIL;
    }
  }
  /* New profiles clear the app bank only after a verified full-device backup.
   * Mirror recovery can still restore the old table after interrupted writes.
   */
  if (err == ESP_OK && sizes) {
    for (uint32_t off = 0x210000; off < 0xa10000; off += 65536) {
      err = esp_flash_erase_region(NULL, off, 65536);
      if (err != ESP_OK)
        break;
      if (cb)
        cb(95, "Reparticionando aplicativos: não interrompa", user);
      vTaskDelay(1);
    }
  }
  if (err == ESP_OK && cb)
    cb(96, "Gravando tabela: nao interrompa", user);
  if (err == ESP_OK)
    err = layout_table_publish(b + 4096);
  if (err == ESP_OK) {
    err = esp_flash_read(NULL, b, 0x8000, 4096);
    if (err == ESP_OK && memcmp(b, b + 4096, 4096))
      err = ESP_FAIL;
  }
  if (err == ESP_OK) {
    uint32_t zero = 0;
    err = esp_flash_write(NULL, &zero, LAYOUT_TX_OFFSET, sizeof(zero));
  }
  free(b);
  if (err == ESP_OK) {
    if (cb)
      cb(100, "Tabela verificada. Reiniciando para continuar", user);
    snprintf(message, size,
             "Layout pronto; reinicie para continuar pelo arquivo salvo.");
  } else
    snprintf(message, size,
             "Falha na tabela. Reinicie para recuperar. Backup integral em "
             "/backups/layout-before.bin; recuperacao externa se necessario.");
  return err;
}

esp_err_t partitions_mgr_apply_big(const uint8_t e[32],
                                   partition_progress_fn cb, void *u, char *m,
                                   unsigned n) {
  return apply_layout(e, NULL, 0, cb, u, m, n);
}
esp_err_t partitions_mgr_apply_profile(const uint8_t e[32], const uint32_t *s,
                                       unsigned count, partition_progress_fn cb,
                                       void *u, char *m, unsigned n) {
  return apply_layout(e, s, count, cb, u, m, n);
}
