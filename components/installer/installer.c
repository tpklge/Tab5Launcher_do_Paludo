#include "installer.h"
#include "app_data.h"
#include "board.h"
#include "boot_mgr.h"
#include "catalog_tracking.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_flash_encrypt.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_secure_boot.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "flash_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "launcher_state.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include "partitions_mgr.h"
#include "storage_guard.h"
#include "wifi_mgr.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *TAG = "installer";
static SemaphoreHandle_t s_install_lock;
static atomic_bool s_busy;
bool installer_is_busy(void) { return atomic_load(&s_busy); }
typedef struct {
  uint32_t schema;
  char path[SD_PATH_MAX];
  uint8_t source_sha256[32];
} pending_t;
static bool pending_read(pending_t *p) {
  launcher_state_handle_t h;
  if (launcher_state_open("install_state", LAUNCHER_STATE_READONLY, &h) !=
      ESP_OK)
    return false;
  size_t n = sizeof(*p);
  esp_err_t err = launcher_state_get_blob(h, "pending", p, &n);
  launcher_state_close(h);
  return err == ESP_OK && n == sizeof(*p) && p->schema == 1 &&
         memchr(p->path, 0, sizeof(p->path)) && sd_path_valid(p->path);
}
bool installer_pending_source(char *path, size_t size) {
  pending_t p;
  if (!pending_read(&p))
    return false;
  snprintf(path, size, "%s", p.path);
  return true;
}
static void pending_clear(void) {
  launcher_state_handle_t h;
  if (launcher_state_open("install_state", LAUNCHER_STATE_READWRITE, &h) ==
      ESP_OK) {
    launcher_state_erase_key(h, "pending");
    launcher_state_commit(h);
    launcher_state_close(h);
  }
}
bool installer_recovery_required(void) {
  if (flash_operation_pending() || app_data_recovery_required())
    return true;
  launcher_state_handle_t h;
  esp_err_t e =
      launcher_state_open("install_state", LAUNCHER_STATE_READONLY, &h);
  if (e == ESP_ERR_NOT_FOUND)
    return false;
  if (e)
    return true;
  uint8_t phase = 0;
  e = launcher_state_get_u8(h, "phase", &phase);
  launcher_state_close(h);
  if (e == ESP_ERR_NOT_FOUND)
    return false;
  return e != ESP_OK || phase == 1 || phase > 2;
}
esp_err_t installer_cancel_pending(const char *source) {
  if (!source || !s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  pending_t pending;
  esp_err_t err = ESP_OK;
  if (pending_read(&pending) && !strcmp(source, pending.path)) {
    launcher_state_handle_t h;
    err = launcher_state_open("install_state", LAUNCHER_STATE_READWRITE, &h);
    if (err == ESP_OK) {
      err = launcher_state_erase_key(h, "pending");
      if (err == ESP_OK)
        err = launcher_state_commit(h);
      launcher_state_close(h);
    }
  }
  xSemaphoreGive(s_install_lock);
  return err;
}
static esp_err_t partition_digest(const esp_partition_t *part,
                                  uint8_t digest[32]);
static const esp_partition_t *data_target(const firmware_region_t *r) {
  if (strcmp(r->label, "vfs") && strcmp(r->label, "spiffs"))
    return NULL;
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, r->label);
  if (!p || p->readonly || p->encrypted ||
      (p->subtype != 0x81 && p->subtype != 0x82) || r->length > p->size ||
      (p->address != 0xa10000 && p->address != 0xf16000))
    return NULL;
  return p;
}
static esp_err_t journal(uint8_t phase, const char *path) {
  launcher_state_handle_t h;
  esp_err_t err =
      launcher_state_open("install_state", LAUNCHER_STATE_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = launcher_state_set_str(h, "source", path);
  if (err == ESP_OK)
    err = launcher_state_set_u8(h, "phase", phase);
  if (err == ESP_OK)
    err = launcher_state_commit(h);
  launcher_state_close(h);
  return err;
}
esp_err_t installer_init(void) {
  if (!s_install_lock)
    s_install_lock = xSemaphoreCreateMutex();
  if (!s_install_lock)
    return ESP_ERR_NO_MEM;
  launcher_state_handle_t h;
  if (launcher_state_open("install_state", LAUNCHER_STATE_READONLY, &h) ==
      ESP_OK) {
    uint8_t phase = 0;
    launcher_state_get_u8(h, "phase", &phase);
    launcher_state_close(h);
    if (phase == 1)
      ESP_LOGW(TAG,
               "previous install interrupted; target is not selected for boot");
  }
  return ESP_OK;
}
static esp_err_t partition_empty(const esp_partition_t *part, bool *empty) {
  uint8_t *bytes = malloc(4096);
  if (!bytes)
    return ESP_ERR_NO_MEM;
  *empty = true;
  esp_err_t err = ESP_OK;
  for (uint32_t off = 0; off < part->size && *empty; off += 4096) {
    size_t n = part->size - off < 4096 ? part->size - off : 4096;
    err = esp_partition_read(part, off, bytes, n);
    if (err)
      break;
    for (size_t i = 0; i < n; i++)
      if (bytes[i] != 255) {
        *empty = false;
        break;
      }
    if (!(off & 65535))
      vTaskDelay(1);
  }
  free(bytes);
  return err;
}

esp_err_t installer_plan(const char *path, install_plan_t *plan, char *error,
                         size_t size) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  if (!plan || !sd_path_valid(path))
    return ESP_ERR_INVALID_ARG;
  memset(plan, 0, sizeof(*plan));
  plan->slot = -1;
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!running || running->address != 0x20000)
    return ESP_ERR_INVALID_STATE;
  if (esp_flash_encryption_enabled() || esp_secure_boot_enabled()) {
    snprintf(
        error, size,
        "Instalador ainda nao validado com Secure Boot/flash criptografada");
    return ESP_ERR_NOT_SUPPORTED;
  }
  esp_err_t err = firmware_analyze_file(path, &plan->firmware, error, size);
  if (err != ESP_OK)
    return err;
  pending_t pending;
  if (pending_read(&pending) && !strcmp(path, pending.path) &&
      memcmp(pending.source_sha256, plan->firmware.source_sha256, 32)) {
    snprintf(error, size,
             "Origem pendente mudou; restaure o arquivo original antes de "
             "continuar.");
    return ESP_ERR_INVALID_STATE;
  }
  for (unsigned i = 0; i < plan->firmware.data_count; i++) {
    const firmware_region_t *r = &plan->firmware.data[i];
    const esp_partition_t *p = data_target(r);
    if (!p || (plan->firmware.kind == FW_MERGED && r->offset != p->address) ||
        partition_digest(p, plan->data_sha256[i]) != ESP_OK) {
      snprintf(error, size, "Particao de dados incompativel ou protegida: %s",
               r->label);
      return ESP_ERR_NOT_SUPPORTED;
    }
  }
  if (plan->firmware.kind == FW_DATA_BACKUP) {
    const esp_partition_t *data = data_target(&plan->firmware.data[0]);
    if (!data || data->size != plan->firmware.data[0].length)
      return ESP_ERR_INVALID_STATE;
    snprintf(plan->path, sizeof(plan->path), "%s", path);
    snprintf(plan->target, sizeof(plan->target), "%s", data->label);
    plan->address = data->address;
    plan->size = data->size;
    return ESP_OK;
  }
  uint32_t required = plan->firmware.kind == FW_RAW_BACKUP
                          ? plan->firmware.app.length
                          : plan->firmware.image.image_size;
  flash_allocation_t allocation;
  err = flash_allocate_plan(required, &allocation, error, size);
  if (err)
    return err;
  snprintf(plan->path, sizeof(plan->path), "%s", path);
  plan->slot = allocation.slot;
  snprintf(plan->target, sizeof(plan->target), "app%d", plan->slot);
  plan->address = allocation.address;
  plan->size = allocation.size;
  plan->needs_layout = allocation.changed;
  memcpy(plan->layout_sha256, allocation.digest, 32);
  const esp_partition_t *selected = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + plan->slot,
      NULL);
  if (!plan->needs_layout) {
    if (!selected || selected->readonly || selected->encrypted ||
        selected->address != plan->address || selected->size != plan->size)
      return ESP_ERR_INVALID_STATE;
    bool empty = false;
    if (partition_empty(selected, &empty) || !empty)
      return ESP_ERR_INVALID_STATE;
    if (partition_digest(selected, plan->target_sha256))
      return ESP_FAIL;
  }
  plan->replaces_app = false;
  return ESP_OK;
}

static esp_err_t backup(const esp_partition_t *part,
                        installer_progress_fn progress, void *user,
                        char *message, size_t size) {
  if (mkdir(LAUNCHER_ROOT "/backups", 0775) != 0 && errno != EEXIST)
    return ESP_FAIL;
  char path[128], temp[144];
  snprintf(path, sizeof(path), LAUNCHER_ROOT "/backups/%s-%lld.raw",
           part->label, (long long)esp_timer_get_time());
  snprintf(temp, sizeof(temp), "%s.part", path);
  struct stat existing;
  if (stat(path, &existing) == 0 || stat(temp, &existing) == 0)
    return ESP_ERR_INVALID_STATE;
  FILE *out = fopen(temp, "wb");
  if (!out)
    return ESP_FAIL;
  uint8_t buffer[4096], expected[32], actual[32];
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  esp_err_t err = ESP_OK;
  for (uint32_t offset = 0; offset < part->size; offset += sizeof(buffer)) {
    size_t n = part->size - offset < sizeof(buffer) ? part->size - offset
                                                    : sizeof(buffer);
    if (esp_partition_read(part, offset, buffer, n) != ESP_OK ||
        fwrite(buffer, 1, n, out) != n) {
      err = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&hash, buffer, n);
    if (offset % 65536 == 0) {
      if (progress &&
          !progress((offset * 20u) / part->size, "Backup do app atual", user)) {
        err = ESP_ERR_INVALID_STATE;
        break;
      }
      vTaskDelay(1);
    }
  }
  mbedtls_sha256_finish(&hash, expected);
  mbedtls_sha256_free(&hash);
  if (fflush(out) != 0 || fsync(fileno(out)) != 0)
    err = ESP_FAIL;
  if (fclose(out) != 0)
    err = ESP_FAIL;
  if (err == ESP_OK) {
    FILE *in = fopen(temp, "rb");
    if (!in)
      err = ESP_FAIL;
    else {
      mbedtls_sha256_init(&hash);
      mbedtls_sha256_starts(&hash, 0);
      size_t n;
      uint32_t total = 0;
      while ((n = fread(buffer, 1, sizeof(buffer), in))) {
        mbedtls_sha256_update(&hash, buffer, n);
        total += n;
        vTaskDelay(1);
      }
      if (ferror(in) || total != part->size)
        err = ESP_FAIL;
      fclose(in);
      mbedtls_sha256_finish(&hash, actual);
      mbedtls_sha256_free(&hash);
      if (memcmp(actual, expected, 32))
        err = ESP_FAIL;
    }
  }
  if (err == ESP_OK && rename(temp, path) != 0)
    err = ESP_FAIL;
  if (err == ESP_OK) {
    char metadata[144], pending[160], hex[65];
    snprintf(metadata, sizeof(metadata), "%s.json", path);
    snprintf(pending, sizeof(pending), "%s.part", metadata);
    for (unsigned i = 0; i < 32; i++)
      snprintf(hex + 2 * i, 3, "%02x", expected[i]);
    FILE *manifest = fopen(pending, "w");
    if (!manifest)
      err = ESP_FAIL;
    else {
      fprintf(manifest,
              "{\n  \"schema\": 1,\n  \"kind\": \"partition-backup\",\n  "
              "\"offset\": %lu,\n  \"size\": %lu,\n  \"sha256\": \"%s\"\n}\n",
              (unsigned long)part->address, (unsigned long)part->size, hex);
      bool failed = ferror(manifest) || fflush(manifest) != 0 ||
                    fsync(fileno(manifest)) != 0;
      if (fclose(manifest) != 0)
        failed = true;
      if (failed || rename(pending, metadata) != 0) {
        remove(pending);
        err = ESP_FAIL;
      }
    }
  }
  if (err != ESP_OK)
    remove(temp);
  else {
    ESP_LOGI(TAG, "verified slot backup: %s", path);
    snprintf(message, size, "Backup: %s", path);
  }
  return err;
}
static esp_err_t write_data(const install_plan_t *plan,
                            installer_progress_fn progress, void *user) {
  esp_err_t err = ESP_OK;
  uint8_t *buffer = malloc(4096), digest[32];
  if (!buffer)
    return ESP_ERR_NO_MEM;
  mbedtls_sha256_context hash;
  for (unsigned i = 0; i < plan->firmware.data_count && err == ESP_OK; i++) {
    const firmware_region_t *r = &plan->firmware.data[i];
    const esp_partition_t *data = data_target(r);
    uint8_t actual[32];
    if (!data || firmware_hash_region(r, actual) != ESP_OK ||
        memcmp(actual, r->sha256, 32)) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    if (progress &&
        !progress(92, "Preparando dados: ultimo cancelamento", user)) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    FILE *in = fopen(r->path, "rb");
    if (!in) {
      err = ESP_FAIL;
      break;
    }
    if (fseek(in, r->offset, SEEK_SET))
      err = ESP_FAIL;
    if (err == ESP_OK && progress)
      progress(92, "Gravando dados: nao interrompa", user);
    if (err == ESP_OK)
      err = esp_partition_erase_range(data, 0, data->size);
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    for (uint32_t off = 0; off < r->length && err == ESP_OK; off += 4096) {
      size_t n = r->length - off < 4096 ? r->length - off : 4096;
      if (fread(buffer, 1, n, in) != n) {
        err = ESP_FAIL;
        break;
      }
      mbedtls_sha256_update(&hash, buffer, n);
      err = esp_partition_write(data, off, buffer, n);
      vTaskDelay(1);
      if (progress)
        progress(92 + (unsigned)(((uint64_t)off + n) * 4 / r->length),
                 "Gravando dados: nao interrompa", user);
    }
    fclose(in);
    mbedtls_sha256_finish(&hash, digest);
    mbedtls_sha256_free(&hash);
    if (err == ESP_OK && memcmp(digest, r->sha256, 32))
      err = ESP_FAIL;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_starts(&hash, 0);
    for (uint32_t off = 0; off < r->length && err == ESP_OK; off += 4096) {
      size_t n = r->length - off < 4096 ? r->length - off : 4096;
      err = esp_partition_read(data, off, buffer, n);
      if (err == ESP_OK)
        mbedtls_sha256_update(&hash, buffer, n);
      vTaskDelay(1);
    }
    mbedtls_sha256_finish(&hash, actual);
    mbedtls_sha256_free(&hash);
    if (err == ESP_OK && memcmp(actual, r->sha256, 32))
      err = ESP_FAIL;
  }
  free(buffer);
  return err;
}
static esp_err_t stage_pending_source(const install_plan_t *plan) {
  char dir[192];
  snprintf(dir, sizeof(dir),
           LAUNCHER_ROOT "/incoming/install-%02x%02x%02x%02x%02x%02x%02x%02x",
           plan->firmware.source_sha256[0], plan->firmware.source_sha256[1],
           plan->firmware.source_sha256[2], plan->firmware.source_sha256[3],
           plan->firmware.source_sha256[4], plan->firmware.source_sha256[5],
           plan->firmware.source_sha256[6], plan->firmware.source_sha256[7]);
  mkdir(LAUNCHER_ROOT "/incoming", 0755);
  mkdir(dir, 0755);
  const char *sources[FW_DATA_MAX + 2] = {plan->path, plan->firmware.app.path};
  for (unsigned i = 0; i < plan->firmware.data_count; i++)
    sources[i + 2] = plan->firmware.data[i].path;
  for (unsigned i = 0; i < FW_DATA_MAX + 2; i++) {
    if (!sources[i] || !*sources[i])
      continue;
    bool duplicate = false;
    for (unsigned j = 0; j < i; j++)
      if (sources[j] && !strcmp(sources[i], sources[j]))
        duplicate = true;
    if (duplicate)
      continue;
    int e = sd_files_copy(sources[i], dir);
    if (e && e != EEXIST)
      return ESP_FAIL;
  }
  if (plan->firmware.kind == FW_RAW_BACKUP) {
    char sidecar[SD_PATH_MAX];
    int n = snprintf(sidecar, sizeof(sidecar), "%s.json", plan->path);
    if (n < 0 || n >= (int)sizeof(sidecar))
      return ESP_ERR_INVALID_SIZE;
    int err = sd_files_copy(sidecar, dir);
    if (err && err != EEXIST)
      return ESP_FAIL;
  }
  pending_t pending = {.schema = 1};
  const char *name = strrchr(plan->path, '/');
  if (!name || !sd_path_join(pending.path, sizeof(pending.path), dir, name + 1))
    return ESP_ERR_INVALID_ARG;
  firmware_info_t *checked = malloc(sizeof(*checked));
  if (!checked)
    return ESP_ERR_NO_MEM;
  char message[128];
  esp_err_t e =
      firmware_analyze_file(pending.path, checked, message, sizeof(message));
  if (!e && memcmp(checked->source_sha256, plan->firmware.source_sha256, 32))
    e = ESP_ERR_INVALID_CRC;
  if (!e && (checked->data_count != plan->firmware.data_count ||
             memcmp(checked->app.sha256, plan->firmware.app.sha256, 32)))
    e = ESP_ERR_INVALID_CRC;
  for (unsigned i = 0; !e && i < checked->data_count; i++)
    if (checked->data[i].length != plan->firmware.data[i].length ||
        memcmp(checked->data[i].sha256, plan->firmware.data[i].sha256, 32))
      e = ESP_ERR_INVALID_CRC;
  free(checked);
  if (e)
    return e;
  e = catalog_alias_download(plan->path, pending.path);
  if (e && e != ESP_ERR_NOT_FOUND)
    return e;
  memcpy(pending.source_sha256, plan->firmware.source_sha256, 32);
  launcher_state_handle_t h;
  e = launcher_state_open("install_state", LAUNCHER_STATE_READWRITE, &h);
  if (e)
    return e;
  e = launcher_state_set_blob(h, "pending", &pending, sizeof(pending));
  if (!e)
    e = launcher_state_commit(h);
  launcher_state_close(h);
  return e;
}
static esp_err_t execute_locked(const install_plan_t *plan,
                                installer_progress_fn progress, void *user,
                                char *message, size_t size) {
  if (!plan || !sd_path_valid(plan->path) ||
      (plan->slot < 0 && plan->firmware.kind != FW_DATA_BACKUP))
    return ESP_ERR_INVALID_ARG;
  if (!boot_mgr_return_supported()) {
    snprintf(message, size, "Bootloader de retorno ainda nao instalado");
    return ESP_ERR_NOT_SUPPORTED;
  }
  esp_err_t profile_warning = ESP_OK;
  install_plan_t fresh;
  esp_err_t err = installer_plan(plan->path, &fresh, message, size);
  if (err != ESP_OK)
    return err;
  if (fresh.address != plan->address || fresh.size != plan->size ||
      fresh.replaces_app != plan->replaces_app ||
      strcmp(fresh.replaced_project, plan->replaced_project) ||
      memcmp(fresh.target_sha256, plan->target_sha256, 32) ||
      memcmp(&fresh.firmware, &plan->firmware, sizeof(fresh.firmware)) ||
      fresh.needs_layout != plan->needs_layout ||
      memcmp(fresh.layout_sha256, plan->layout_sha256, 32) ||
      memcmp(fresh.displaced_sha256, plan->displaced_sha256, 32) ||
      memcmp(fresh.data_sha256, plan->data_sha256, sizeof(fresh.data_sha256))) {
    snprintf(message, size, "Imagem ou destino mudou. Analise novamente.");
    return ESP_ERR_INVALID_STATE;
  }
  if (plan->firmware.kind == FW_DATA_BACKUP) {
    const esp_partition_t *data = data_target(&plan->firmware.data[0]);
    if (!data || backup(data, progress, user, message, size) != ESP_OK)
      return ESP_FAIL;
    if (progress && !progress(20, "Preparando restauracao", user))
      return ESP_ERR_INVALID_STATE;
    err = app_data_save_active(progress, user, message, size);
    if (!err)
      err = app_data_release_owner();
    if (!err)
      err = journal(1, plan->path);
    if (err == ESP_OK)
      err = write_data(plan, progress, user);
    if (err == ESP_OK)
      err = journal(2, plan->path);
    snprintf(message, size, "%s de %s",
             err == ESP_OK ? "Dados restaurados e verificados"
                           : "Falha ao restaurar dados",
             data->label);
    return err;
  }
  if (plan->needs_layout) {
    esp_err_t pending_error = stage_pending_source(plan);
    if (pending_error != ESP_OK)
      return pending_error;
    uint32_t required = plan->firmware.kind == FW_RAW_BACKUP
                            ? plan->firmware.app.length
                            : plan->firmware.image.image_size;
    esp_err_t pause = wifi_pause_wait(15000);
    if (pause) {
      wifi_resume();
      return pause;
    }
    esp_err_t migration = flash_allocate_apply(required, plan->layout_sha256,
                                               progress, user, message, size);
    if (migration != ESP_OK) {
      if (!flash_operation_pending())
        wifi_resume();
      return migration;
    }
    if (!storage_prepare_reboot(5000)) {
      snprintf(
          message, size,
          "Layout aplicado; arquivos ainda em uso. Aguarde e pressione RESET.");
      return ESP_ERR_INVALID_STATE;
    }
    board_shutdown();
    esp_restart();
    return ESP_ERR_INVALID_STATE;
  }
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + plan->slot,
      NULL);
  if (!part || part->address != plan->address)
    return ESP_ERR_INVALID_STATE;
  uint8_t previous_identity[32];
  bool inherit_profile =
      plan->replaces_app &&
      !strcmp(plan->replaced_project, plan->firmware.image.project) &&
      esp_partition_get_sha256(part, previous_identity) == ESP_OK;
  if (fresh.replaces_app &&
      backup(part, progress, user, message, size) != ESP_OK) {
    snprintf(message, size, "Backup nao concluido; nenhum app apagado");
    return ESP_FAIL;
  }
  for (unsigned i = 0; i < plan->firmware.data_count; i++) {
    const esp_partition_t *data = data_target(&plan->firmware.data[i]);
    if (!data || backup(data, progress, user, message, size) != ESP_OK) {
      snprintf(message, size,
               "Backup dos dados falhou; nenhuma particao de dados apagada.");
      return ESP_FAIL;
    }
  }
  if (progress && !progress(20, "Preparando instalacao", user))
    return ESP_ERR_INVALID_STATE;
  FILE *file = fopen(plan->firmware.app.path, "rb");
  if (!file)
    return ESP_FAIL;
  if (fseek(file, plan->firmware.app.offset, SEEK_SET) != 0) {
    fclose(file);
    return ESP_FAIL;
  }
  if (journal(1, plan->path) != ESP_OK) {
    fclose(file);
    return ESP_FAIL;
  }
  uint8_t *buffer = malloc(4096);
  if (!buffer) {
    fclose(file);
    return ESP_ERR_NO_MEM;
  }
  esp_ota_handle_t handle = 0;
  err = esp_ota_begin(part,
                      plan->firmware.kind == FW_RAW_BACKUP
                          ? part->size
                          : plan->firmware.app.length,
                      &handle);
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  uint8_t digest[32];
  uint32_t total = 0;
  while (err == ESP_OK && total < plan->firmware.app.length) {
    size_t n = plan->firmware.app.length - total;
    if (n > 4096)
      n = 4096;
    if (fread(buffer, 1, n, file) != n) {
      err = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&hash, buffer, n);
    err = esp_ota_write(handle, buffer, n);
    total += n;
    if (progress && !progress(20 + (total * 70u) / plan->firmware.app.length,
                              "Gravando aplicativo", user)) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    vTaskDelay(1);
  }
  mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  fclose(file);
  free(buffer);
  if (err == ESP_OK && memcmp(digest, plan->firmware.sha256, 32))
    err = ESP_FAIL;
  if (err == ESP_OK) {
    err = esp_ota_end(handle);
    handle = 0;
  }
  if (handle)
    esp_ota_abort(handle);
  if (err == ESP_OK) {
    esp_partition_pos_t pos = {.offset = part->address, .size = part->size};
    esp_image_metadata_t meta;
    err = esp_image_verify(ESP_IMAGE_VERIFY, &pos, &meta);
  }
  if (err == ESP_OK) {
    if (plan->firmware.data_count) {
      err = app_data_save_active(progress, user, message, size);
      if (!err)
        err = app_data_release_owner();
    }
    if (!err)
      err = write_data(plan, progress, user);
  }
  if (err == ESP_OK) {
    uint8_t identity[32];
    if (esp_partition_get_sha256(part, identity) == ESP_OK) {
      catalog_track_install(plan->path, identity);
      if (inherit_profile)
        profile_warning = app_data_inherit(previous_identity, identity,
                                           progress, user, message, size);
      if (plan->firmware.data_count) {
        app_data_config_t config = {0};
        app_data_get(identity, &config);
        memcpy(config.identity, identity, 32);
        for (unsigned i = 0; i < plan->firmware.data_count; i++) {
          const esp_partition_t *data = esp_partition_find_first(
              ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
              plan->firmware.data[i].label);
          if (!data)
            continue;
          bool exists = false;
          for (unsigned j = 0; j < config.count; j++)
            if (!strcmp(config.regions[j].label, data->label))
              exists = true;
          if (exists)
            continue;
          if (config.count >= APP_DATA_MAX) {
            profile_warning = ESP_ERR_INVALID_SIZE;
            break;
          }
          app_data_region_t *r = &config.regions[config.count++];
          snprintf(r->label, sizeof(r->label), "%s", data->label);
          r->address = data->address;
          r->size = data->size;
          r->nvs = !strcmp(data->label, "nvs");
        }
        if (!profile_warning)
          profile_warning =
              app_data_configure(&config, true, progress, user, message, size);
      }
      char key[40];
      snprintf(key, sizeof(key), "data-%02x%02x%02x%02x%02x%02x%02x",
               identity[0], identity[1], identity[2], identity[3], identity[4],
               identity[5], identity[6]);
      struct {
        uint8_t identity[32];
        char labels[FW_DATA_MAX][17];
      } map = {0};
      memcpy(map.identity, identity, 32);
      for (unsigned i = 0; i < plan->firmware.data_count; i++)
        snprintf(map.labels[i], 17, "%s", plan->firmware.data[i].label);
      launcher_store_write(key, &map, sizeof(map));
    }
    err = journal(2, plan->path);
  }
  if (err == ESP_OK) {
    pending_clear();
    if (plan->replaces_app && boot_mgr_get_default() == plan->slot)
      boot_mgr_set_default(-1);
    if (boot_mgr_stopped_slot() == plan->slot)
      boot_mgr_clear_stopped();
  }
  if (err != ESP_OK) {
    snprintf(message, size,
             "Instalacao interrompida (%s). Launcher preservado.",
             esp_err_to_name(err));
    return err;
  }
  if (progress)
    progress(100, "Aplicativo verificado", user);
  snprintf(message, size,
           "Instalado e verificado em %s. Dados externos preservados.",
           part->label);
  if (profile_warning)
    snprintf(message, size,
             "App instalado, mas perfil não foi salvo (%s). Configure Dados do "
             "app antes de trocar.",
             esp_err_to_name(profile_warning));
  return ESP_OK;
}

static esp_err_t execute_impl(const install_plan_t *plan,
                              installer_progress_fn progress, void *user,
                              char *message, size_t size) {
  if (flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE) {
    snprintf(message, size, "Outro instalador esta ativo");
    return ESP_ERR_INVALID_STATE;
  }
  atomic_store(&s_busy, true);
  esp_err_t err = execute_locked(plan, progress, user, message, size);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return err;
}

static esp_err_t partition_digest(const esp_partition_t *part,
                                  uint8_t digest[32]) {
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  mbedtls_sha256_starts(&hash, 0);
  uint8_t *buffer = malloc(4096);
  if (!buffer) {
    mbedtls_sha256_free(&hash);
    return ESP_ERR_NO_MEM;
  }
  esp_err_t err = ESP_OK;
  for (uint32_t off = 0; off < part->size; off += 4096) {
    size_t n = part->size - off < 4096 ? part->size - off : 4096;
    err = esp_partition_read(part, off, buffer, n);
    if (err != ESP_OK)
      break;
    mbedtls_sha256_update(&hash, buffer, n);
    if (off % 65536 == 0)
      vTaskDelay(1);
  }
  if (err == ESP_OK)
    mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  free(buffer);
  return err;
}
esp_err_t installer_slot_digest(int slot, uint8_t digest[32]) {
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || !digest)
    return ESP_ERR_INVALID_ARG;
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  return part ? partition_digest(part, digest) : ESP_ERR_NOT_FOUND;
}
static esp_err_t uninstall_locked(int slot, const uint8_t expected[32],
                                  bool backup_binary,
                                  installer_progress_fn progress, void *user,
                                  char *message, size_t size) {
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || !expected)
    return ESP_ERR_INVALID_ARG;
  if (esp_flash_encryption_enabled() || esp_secure_boot_enabled())
    return ESP_ERR_NOT_SUPPORTED;
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!part || part->readonly || part->encrypted ||
      (running && running->address == part->address))
    return ESP_ERR_INVALID_STATE;
  uint8_t actual[32];
  if (installer_slot_digest(slot, actual) != ESP_OK ||
      memcmp(actual, expected, 32)) {
    snprintf(message, size,
             "O slot mudou. Abra a HOME novamente para confirmar.");
    return ESP_ERR_INVALID_STATE;
  }
  if (backup_binary && backup(part, progress, user, message, size) != ESP_OK) {
    snprintf(message, size, "Backup nao concluido. Aplicativo preservado.");
    return ESP_FAIL;
  }
  if (progress && !progress(20, "Preparando remocao", user))
    return ESP_ERR_INVALID_STATE;
  if (journal(1, part->label) != ESP_OK)
    return ESP_FAIL;
  for (uint32_t offset = 0; offset < part->size; offset += 65536) {
    size_t n = part->size - offset < 65536 ? part->size - offset : 65536;
    esp_err_t err = esp_partition_erase_range(part, offset, n);
    if (err != ESP_OK) {
      snprintf(message, size, "Falha ao apagar slot; backup preservado (%s)",
               esp_err_to_name(err));
      return err;
    }
    /* Once erasure begins, finish the slot; cancellation can no longer undo
     * removal. */
    if (progress)
      progress(20 + (unsigned)(((uint64_t)offset + n) * 75 / part->size),
               "Removendo slot: aguarde", user);
    vTaskDelay(1);
  }
  if (boot_mgr_get_default() == slot)
    boot_mgr_set_default(-1);
  if (boot_mgr_stopped_slot() == slot)
    boot_mgr_clear_stopped();
  esp_err_t err = journal(2, part->label);
  if (err != ESP_OK) {
    snprintf(message, size, "Slot removido, mas journal falhou");
    return err;
  }
  if (progress)
    progress(100, "Aplicativo removido", user);
  snprintf(message, size, "%s removido. Dados externos preservados. %s",
           part->label,
           backup_binary ? "Backup do app salvo no SD."
                         : "Sem backup do app, conforme selecionado.");
  return ESP_OK;
}
esp_err_t installer_uninstall(int slot, const uint8_t expected[32],
                              installer_progress_fn progress, void *user,
                              char *message, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t err =
      uninstall_locked(slot, expected, true, progress, user, message, size);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return err;
}

esp_err_t installer_backup_slot(int slot, installer_progress_fn progress,
                                void *user, char *message, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || !message || !size)
    return ESP_ERR_INVALID_ARG;
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  esp_err_t e =
      p ? backup(p, progress, user, message, size) : ESP_ERR_NOT_FOUND;
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

unsigned installer_data_options(
    int slot, installer_data_option_t options[INSTALLER_DATA_OPTIONS]) {
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || !options)
    return 0;
  const esp_partition_t *app = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  struct {
    uint8_t identity[32];
    char labels[FW_DATA_MAX][17];
  } map = {0};
  uint8_t identity[32];
  if (app && esp_partition_get_sha256(app, identity) == ESP_OK) {
    char key[40];
    snprintf(key, sizeof(key), "data-%02x%02x%02x%02x%02x%02x%02x", identity[0],
             identity[1], identity[2], identity[3], identity[4], identity[5],
             identity[6]);
    size_t n = sizeof(map);
    if (launcher_store_read(key, &map, &n) != ESP_OK || n != sizeof(map) ||
        memcmp(identity, map.identity, 32))
      memset(&map, 0, sizeof(map));
  }
  unsigned count = 0;
  esp_partition_iterator_t it = esp_partition_find(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it) {
    const esp_partition_t *p = esp_partition_get(it);
    if (p && strcmp(p->label, "launch_req") &&
        p->subtype != ESP_PARTITION_SUBTYPE_DATA_PHY &&
        p->subtype != ESP_PARTITION_SUBTYPE_DATA_OTA &&
        p->subtype != ESP_PARTITION_SUBTYPE_DATA_COREDUMP &&
        p->subtype != ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS &&
        count < INSTALLER_DATA_OPTIONS) {
      installer_data_option_t *o = &options[count++];
      memset(o, 0, sizeof(*o));
      snprintf(o->label, sizeof(o->label), "%s", p->label);
      o->size = p->size;
      for (unsigned i = 0; i < FW_DATA_MAX; i++)
        if (!strncmp(map.labels[i], p->label, 17))
          o->associated = true;
    }
    it = esp_partition_next(it);
  }
  return count;
}
esp_err_t installer_uninstall_options(int slot, const uint8_t expected[32],
                                      bool backup_binary, uint32_t mask,
                                      installer_progress_fn cb, void *user,
                                      char *message, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || !expected || !message || !size)
    return ESP_ERR_INVALID_ARG;
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  uint8_t actual[32];
  esp_err_t e = installer_slot_digest(slot, actual);
  if (!e && memcmp(actual, expected, 32))
    e = ESP_ERR_INVALID_STATE;
  installer_data_option_t options[INSTALLER_DATA_OPTIONS];
  unsigned count = installer_data_options(slot, options);
  for (unsigned i = 0; !e && i < count; i++)
    if (mask & (1u << i)) {
      const esp_partition_t *p = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, options[i].label);
      e = p ? backup(p, cb, user, message, size) : ESP_ERR_NOT_FOUND;
    }
  if (!e)
    e = uninstall_locked(slot, expected, backup_binary, cb, user, message,
                         size);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_repartition(const uint32_t *sizes, unsigned count,
                                const uint8_t expected[32],
                                installer_progress_fn cb, void *u, char *m,
                                size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  int previous = boot_mgr_get_default();
  uint8_t plan[32];
  esp_err_t e = flash_migrate_plan(sizes, count, plan, m, n);
  if (!e && memcmp(plan, expected, 32))
    e = ESP_ERR_INVALID_STATE;
  if (!e)
    e = boot_mgr_set_default(-1);
  if (!e)
    e = flash_migrate_apply(sizes, count, expected, cb, u, m, n);
  if (e && !flash_operation_pending())
    boot_mgr_set_default(previous);
  if (!e && !storage_prepare_reboot(5000)) {
    e = ESP_ERR_INVALID_STATE;
    snprintf(
        m, n,
        "Flash concluida; arquivos ainda em uso. Aguarde e pressione RESET.");
  }
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_profile_configure(const app_data_config_t *c, bool capture,
                                      installer_progress_fn cb, void *u,
                                      char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (flash_operation_pending())
    return ESP_ERR_INVALID_STATE;
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = app_data_configure(c, capture, cb, u, m, n);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}
esp_err_t installer_prepare_app_data(int slot, installer_progress_fn cb,
                                     void *u, char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (installer_recovery_required() || !s_install_lock ||
      xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = app_data_activate(slot, cb, u, m, n);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}
esp_err_t installer_full_backup(flash_snapshot_t *out, installer_progress_fn cb,
                                void *u, char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = flash_snapshot_create(out, cb, u, m, n);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}
esp_err_t installer_full_restore(const flash_snapshot_t *source,
                                 installer_progress_fn cb, void *u, char *m,
                                 size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = wifi_pause_wait(15000);
  if (!e)
    e = boot_mgr_set_default(-1);
  if (!e)
    e = flash_snapshot_restore(source, cb, u, m, n);
  if (e && !flash_operation_pending())
    wifi_resume();
  if (!e && !storage_prepare_reboot(5000)) {
    e = ESP_ERR_INVALID_STATE;
    snprintf(
        m, n,
        "Flash concluida; arquivos ainda em uso. Aguarde e pressione RESET.");
  }
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}
esp_err_t installer_resume_flash(installer_progress_fn cb, void *u, char *m,
                                 size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = wifi_pause_wait(15000);
  if (!e)
    e = flash_operation_resume(cb, u, m, n);
  if (!e && !storage_prepare_reboot(5000)) {
    e = ESP_ERR_INVALID_STATE;
    snprintf(
        m, n,
        "Flash concluida; arquivos ainda em uso. Aguarde e pressione RESET.");
  }
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_recover_profile(bool rollback, installer_progress_fn cb,
                                    void *u, char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = app_data_recover(rollback, cb, u, m, n);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_capture_profile_return(installer_progress_fn cb, void *u,
                                           char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = app_data_capture_return(cb, u, m, n);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_maintenance(esp_err_t (*fn)(void *), void *arg) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!fn || flash_operation_pending() || !s_install_lock ||
      xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = fn(arg);
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_execute(const install_plan_t *p, installer_progress_fn cb,
                            void *u, char *m, size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!p)
    return ESP_ERR_INVALID_ARG;
  if (!sd_media_acquire(p->path))
    return ESP_ERR_INVALID_STATE;
  esp_err_t e = execute_impl(p, cb, u, m, n);
  sd_media_release(p->path);
  return e;
}

esp_err_t installer_uninstall_data(int slot, const uint8_t expected[32],
                                   uint32_t mask, installer_progress_fn cb,
                                   void *u, char *m, size_t n) {
  return installer_uninstall_options(slot, expected, true, mask, cb, u, m, n);
}

esp_err_t installer_compact(const uint8_t expected[32],
                            installer_progress_fn cb, void *u, char *m,
                            size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = wifi_pause_wait(15000);
  if (!e)
    e = flash_allocate_apply(0, expected, cb, u, m, n);
  if (e)
    wifi_resume();
  if (!e && !storage_prepare_reboot(5000)) {
    e = ESP_ERR_INVALID_STATE;
    snprintf(
        m, n,
        "Flash concluida; arquivos ainda em uso. Aguarde e pressione RESET.");
  }
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}

esp_err_t installer_layout_apply(const uint32_t sizes[16],
                                 const uint8_t expected[32],
                                 installer_progress_fn cb, void *u, char *m,
                                 size_t n) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!s_install_lock || xSemaphoreTake(s_install_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_busy, true);
  esp_err_t e = wifi_pause_wait(15000);
  if (!e)
    e = flash_layout_apply(sizes, expected, cb, u, m, n);
  if (e && !flash_operation_pending())
    wifi_resume();
  if (!e && !storage_prepare_reboot(5000)) {
    e = ESP_ERR_INVALID_STATE;
    snprintf(
        m, n,
        "Flash concluida; arquivos ainda em uso. Aguarde e pressione RESET.");
  }
  atomic_store(&s_busy, false);
  xSemaphoreGive(s_install_lock);
  return e;
}
