#include "app_data.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "launcher_state.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define ROOT LAUNCHER_PRIVATE "/profiles"
typedef struct {
  app_data_config_t config;
  char directory[192];
  uint8_t digest[APP_DATA_MAX][32];
} profile_t;
typedef struct {
  uint32_t schema, active;
  uint8_t identity[32];
} owner_t;
typedef struct {
  uint32_t schema;
  profile_t target, baseline;
  owner_t previous;
} frozen_t;
static esp_err_t marker(bool, const uint8_t *, const profile_t *);
static esp_err_t frozen_profile(const uint8_t *, bool, profile_t *, owner_t *);
static esp_err_t owner_read(owner_t *out) {
  size_t size = sizeof(*out);
  esp_err_t e = launcher_store_read("profile-owner", out, &size);
  return !e && (size != sizeof(*out) || out->schema != 1 || out->active > 1)
             ? ESP_ERR_INVALID_CRC
             : e;
}
static esp_err_t owner_write(const owner_t *owner) {
  return launcher_store_write("profile-owner", owner, sizeof(*owner));
}
static esp_err_t target_read(uint8_t id[32]) {
  launcher_state_handle_t h;
  esp_err_t e = launcher_state_open("boot_mgr", LAUNCHER_STATE_READONLY, &h);
  if (e)
    return e;
  uint8_t active = 0;
  size_t size = 32;
  e = launcher_state_get_u8(h, "profile_tx", &active);
  if (!e && active == 1)
    e = launcher_state_get_blob(h, "profile_target", id, &size);
  else if (!e)
    e = active ? ESP_ERR_INVALID_CRC : ESP_ERR_NOT_FOUND;
  launcher_state_close(h);
  return !e && size != 32 ? ESP_ERR_INVALID_CRC : e;
}
static void key_for(const uint8_t *id, char out[48]) {
  snprintf(out, 48, "profile-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7], id[8], id[9],
           id[10], id[11]);
}
static bool valid_region(const app_data_region_t *r) {
  if (!memchr(r->label, 0, sizeof(r->label)) || !r->label[0])
    return false;
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, r->label);
  return p && p->address == r->address && p->size == r->size && !p->encrypted &&
         !p->readonly && p->address >= 0x9000 && p->size && !(p->size & 4095) &&
         (p->address >= 0x20000 ||
          (!strcmp(p->label, "nvs") && p->address == 0x9000 &&
           p->size == 0x6000)) &&
         strcmp(p->label, "launch_req") &&
         p->subtype != ESP_PARTITION_SUBTYPE_DATA_PHY &&
         p->subtype != ESP_PARTITION_SUBTYPE_DATA_OTA &&
         p->subtype != ESP_PARTITION_SUBTYPE_DATA_COREDUMP &&
         p->subtype != ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS &&
         r->nvs == (!strcmp(p->label, "nvs"));
}
static esp_err_t profile_valid(const profile_t *p, const uint8_t *id) {
  if (memcmp(id, p->config.identity, 32) || p->config.count > APP_DATA_MAX ||
      !memchr(p->directory, 0, sizeof(p->directory)) ||
      strncmp(p->directory, ROOT "/", strlen(ROOT) + 1) ||
      strstr(p->directory, "..") || strchr(p->directory, '\\'))
    return ESP_ERR_INVALID_CRC;
  for (unsigned i = 0; i < p->config.count; i++)
    if (!valid_region(&p->config.regions[i]))
      return ESP_ERR_INVALID_CRC;
  return ESP_OK;
}
static esp_err_t load(const uint8_t *id, profile_t *p) {
  char key[48];
  key_for(id, key);
  size_t n = sizeof(*p);
  esp_err_t e = launcher_store_read(key, p, &n);
  if (e)
    return e;
  if (n != sizeof(*p))
    return ESP_ERR_INVALID_SIZE;
  return profile_valid(p, id);
}
static void frozen_path(const uint8_t hash[32], char path[192]) {
  char hex[65];
  for (unsigned i = 0; i < 32; i++)
    snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  snprintf(path, 192, ROOT "/tx-%s.record", hex);
}
static esp_err_t frozen_read(frozen_t *q) {
  launcher_state_handle_t h;
  esp_err_t e = launcher_state_open("boot_mgr", LAUNCHER_STATE_READONLY, &h);
  if (e)
    return e;
  uint8_t active = 0;
  e = launcher_state_get_u8(h, "profile_tx", &active);
  if (e || !active) {
    launcher_state_close(h);
    return e == ESP_ERR_NOT_FOUND || (!e && !active) ? ESP_ERR_NOT_FOUND : e;
  }
  uint8_t hash[32], actual[32];
  size_t size = 32;
  e = launcher_state_get_blob(h, "profile_hash", hash, &size);
  launcher_state_close(h);
  if (e || size != 32)
    return ESP_ERR_INVALID_CRC;
  char path[192];
  frozen_path(hash, path);
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_ERR_INVALID_CRC;
  bool ok =
      fread(q, 1, sizeof(*q), f) == sizeof(*q) && fgetc(f) == EOF && !ferror(f);
  fclose(f);
  if (!ok)
    return ESP_ERR_INVALID_CRC;
  mbedtls_sha256((const uint8_t *)q, sizeof(*q), actual, 0);
  if (memcmp(hash, actual, 32) || q->schema != 1 || q->previous.schema != 1 ||
      q->previous.active > 1)
    return ESP_ERR_INVALID_CRC;
  uint8_t material[33], baseline[32];
  material[0] = 0xb1;
  memcpy(material + 1, q->target.config.identity, 32);
  mbedtls_sha256(material, sizeof(material), baseline, 0);
  if (profile_valid(&q->target, q->target.config.identity) ||
      profile_valid(&q->baseline, baseline))
    return ESP_ERR_INVALID_CRC;
  return ESP_OK;
}
static esp_err_t frozen_profile(const uint8_t *id, bool rollback, profile_t *p,
                                owner_t *previous) {
  frozen_t *q = malloc(sizeof(*q));
  if (!q)
    return ESP_ERR_NO_MEM;
  esp_err_t e = frozen_read(q);
  if (!e && memcmp(id, q->target.config.identity, 32))
    e = ESP_ERR_INVALID_STATE;
  if (!e) {
    *p = rollback ? q->baseline : q->target;
    if (previous)
      *previous = q->previous;
  }
  free(q);
  return e;
}
static esp_err_t marker(bool active, const uint8_t *target,
                        const profile_t *p) {
  uint8_t hash[32];
  esp_err_t e = ESP_OK;
  if (active) {
    frozen_t *q = calloc(1, sizeof(*q));
    if (!q)
      return ESP_ERR_NO_MEM;
    e = frozen_read(q);
    if (e == ESP_ERR_NOT_FOUND) {
      q->schema = 1;
      q->target = *p;
      uint8_t material[33], id[32];
      material[0] = 0xb1;
      memcpy(material + 1, target, 32);
      mbedtls_sha256(material, sizeof(material), id, 0);
      e = load(id, &q->baseline);
      q->previous = (owner_t){.schema = 1};
      if (!e) {
        esp_err_t own = owner_read(&q->previous);
        if (own != ESP_OK && own != ESP_ERR_NOT_FOUND)
          e = own;
      }
      if (!e) {
        mbedtls_sha256((const uint8_t *)q, sizeof(*q), hash, 0);
        char path[192];
        frozen_path(hash, path);
        e = launcher_file_write(path, q, sizeof(*q));
      }
    } else if (!e) {
      if (memcmp(&q->target, p, sizeof(*p)))
        e = ESP_ERR_INVALID_STATE;
      else
        mbedtls_sha256((const uint8_t *)q, sizeof(*q), hash, 0);
    }
    free(q);
    if (e)
      return e;
  }
  launcher_state_handle_t h;
  e = launcher_state_open("boot_mgr", LAUNCHER_STATE_READWRITE, &h);
  if (e)
    return e;
  if (target)
    e = launcher_state_set_blob(h, "profile_target", target, 32);
  if (!e && active)
    e = launcher_state_set_blob(h, "profile_hash", hash, 32);
  if (!e)
    e = launcher_state_set_u8(h, "profile_tx", active ? 1 : 0);
  if (!e)
    e = launcher_state_commit(h);
  launcher_state_close(h);
  return e;
}
bool app_data_get(const uint8_t id[32], app_data_config_t *out) {
  profile_t *p = malloc(sizeof(*p));
  if (!p)
    return false;
  bool ok = load(id, p) == ESP_OK;
  if (ok && out)
    *out = p->config;
  free(p);
  return ok;
}
static esp_err_t hash_file(const char *path, uint8_t digest[32]) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return ESP_FAIL;
  uint8_t *buf = malloc(4096);
  if (!buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  size_t n;
  while ((n = fread(buf, 1, 4096, f)))
    mbedtls_sha256_update(&h, buf, n);
  esp_err_t e = ferror(f) ? ESP_FAIL : ESP_OK;
  mbedtls_sha256_finish(&h, digest);
  mbedtls_sha256_free(&h);
  free(buf);
  fclose(f);
  return e;
}
static esp_err_t snapshot(const app_data_config_t *c, bool empty,
                          app_data_progress_fn cb, void *u, char *m, size_t n) {
  if (launcher_store_init())
    return ESP_ERR_INVALID_STATE;
  mkdir(ROOT, 0700);
  profile_t *p = calloc(1, sizeof(*p));
  if (!p)
    return ESP_ERR_NO_MEM;
  p->config = *c;
  char key[48];
  key_for(c->identity, key);
  snprintf(p->directory, sizeof(p->directory), ROOT "/%s-%lld", key,
           (long long)esp_timer_get_time());
  if (mkdir(p->directory, 0700)) {
    free(p);
    return ESP_FAIL;
  }
  uint8_t *buf = malloc(4096);
  esp_err_t e = buf ? ESP_OK : ESP_ERR_NO_MEM;
  for (unsigned i = 0; !e && i < c->count; i++) {
    const app_data_region_t *r = &c->regions[i];
    if (!valid_region(r)) {
      e = ESP_ERR_INVALID_ARG;
      break;
    }
    char path[240];
    snprintf(path, sizeof(path), "%s/%u.raw", p->directory, i);
    {
      mbedtls_sha256_context original;
      mbedtls_sha256_init(&original);
      mbedtls_sha256_starts(&original, 0);
      FILE *f = fopen(path, "wx");
      if (!f) {
        mbedtls_sha256_free(&original);
        e = ESP_FAIL;
        break;
      }
      {
        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, r->label);
        for (uint32_t off = 0; !e && off < r->size;) {
          unsigned bytes = r->size - off > 4096 ? 4096 : r->size - off;
          if (empty)
            memset(buf, 255, bytes);
          else
            e = esp_partition_read(part, off, buf, bytes);
          if (!e)
            mbedtls_sha256_update(&original, buf, bytes);
          if (!e && fwrite(buf, 1, bytes, f) != bytes)
            e = ESP_FAIL;
          off += bytes;
          if (cb && !cb((i * 100 + (uint64_t)off * 100 / r->size) / c->count,
                        "Salvando perfil de dados", u))
            e = ESP_ERR_INVALID_STATE;
          if (!(off & 65535))
            vTaskDelay(1);
        }
      }
      if (fflush(f) || fsync(fileno(f)))
        e = ESP_FAIL;
      if (fclose(f))
        e = ESP_FAIL;
      uint8_t expected[32], actual[32];
      mbedtls_sha256_finish(&original, expected);
      mbedtls_sha256_free(&original);
      if (!e)
        e = hash_file(path, actual);
      if (!e && memcmp(expected, actual, 32))
        e = ESP_ERR_INVALID_CRC;
    }
    if (!e)
      e = hash_file(path, p->digest[i]);
  }
  free(buf);
  if (!e)
    e = launcher_store_write(key, p, sizeof(*p));
  if (e)
    snprintf(m, n, "Perfil não publicado; dados atuais preservados (%s)",
             esp_err_to_name(e));
  free(p);
  return e;
}
esp_err_t app_data_configure(const app_data_config_t *c, bool capture,
                             app_data_progress_fn cb, void *u, char *m,
                             size_t n) {
  if (!c || c->count > APP_DATA_MAX)
    return ESP_ERR_INVALID_ARG;
  for (unsigned i = 0; i < c->count; i++) {
    if (!valid_region(&c->regions[i]))
      return ESP_ERR_INVALID_ARG;
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(c->regions[i].label, c->regions[j].label))
        return ESP_ERR_INVALID_ARG;
  }
  esp_err_t e = snapshot(c, !capture, cb, u, m, n);
  if (!e)
    snprintf(
        m, n,
        "Perfil salvo: %u partição(ões). A troca de app restaura esse perfil.",
        c->count);
  return e;
}
esp_err_t app_data_save_active(app_data_progress_fn cb, void *u, char *m,
                               size_t n) {
  owner_t owner;
  esp_err_t e = owner_read(&owner);
  if (e == ESP_ERR_NOT_FOUND)
    return ESP_OK;
  if (e)
    return e;
  if (!owner.active)
    return ESP_OK;
  app_data_config_t *config = malloc(sizeof(*config));
  if (!config)
    return ESP_ERR_NO_MEM;
  bool exists = app_data_get(owner.identity, config);
  e = exists ? snapshot(config, false, cb, u, m, n) : ESP_ERR_INVALID_STATE;
  free(config);
  return e;
}
bool app_data_recovery_required(void) {
  launcher_state_handle_t h;
  esp_err_t e = launcher_state_open("boot_mgr", LAUNCHER_STATE_READONLY, &h);
  if (e == ESP_ERR_NOT_FOUND)
    return false;
  if (e)
    return true;
  uint8_t active = 0;
  e = launcher_state_get_u8(h, "profile_tx", &active);
  launcher_state_close(h);
  return e != ESP_ERR_NOT_FOUND && (e != ESP_OK || active != 0);
}
static esp_err_t restore_profile(const profile_t *p, app_data_progress_fn cb,
                                 void *u) {
  uint8_t *buf = malloc(4096);
  if (!buf)
    return ESP_ERR_NO_MEM;
  esp_err_t e = ESP_OK;
  /* Validate every snapshot before touching any partition. */
  for (unsigned i = 0; !e && i < p->config.count; i++) {
    char path[240];
    uint8_t digest[32];
    snprintf(path, sizeof(path), "%s/%u.raw", p->directory, i);
    e = hash_file(path, digest);
    if (!e && memcmp(digest, p->digest[i], 32))
      e = ESP_ERR_INVALID_CRC;
    if (!e) {
      {
        struct stat st;
        if (stat(path, &st) || st.st_size != p->config.regions[i].size)
          e = ESP_ERR_INVALID_SIZE;
      }
    }
  }
  for (unsigned i = 0; !e && i < p->config.count; i++) {
    const app_data_region_t *r = &p->config.regions[i];
    char path[240];
    snprintf(path, sizeof(path), "%s/%u.raw", p->directory, i);
    {
      const esp_partition_t *part = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, r->label);
      FILE *f = fopen(path, "rb");
      if (!f) {
        e = ESP_FAIL;
        break;
      }
      mbedtls_sha256_context h;
      mbedtls_sha256_init(&h);
      mbedtls_sha256_starts(&h, 0);
      for (uint32_t off = 0; !e && off < r->size; off += 4096) {
        if (fread(buf, 1, 4096, f) != 4096) {
          e = ESP_FAIL;
          break;
        }
        e = esp_partition_erase_range(part, off, 4096);
        if (!e)
          e = esp_partition_write(part, off, buf, 4096);
        if (!e)
          e = esp_partition_read(part, off, buf, 4096);
        if (!e)
          mbedtls_sha256_update(&h, buf, 4096);
        if (cb)
          cb((i * 100 + (uint64_t)(off + 4096) * 100 / r->size) /
                 p->config.count,
             "Restaurando perfil; não interrompa", u);
        if (!(off & 65535))
          vTaskDelay(1);
      }
      uint8_t digest[32];
      mbedtls_sha256_finish(&h, digest);
      mbedtls_sha256_free(&h);
      fclose(f);
      if (!e && memcmp(digest, p->digest[i], 32))
        e = ESP_ERR_INVALID_CRC;
    }
  }
  free(buf);
  return e;
}
esp_err_t app_data_activate(int slot, app_data_progress_fn cb, void *u, char *m,
                            size_t n) {
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  uint8_t id[32];
  if (!part || esp_partition_get_sha256(part, id))
    return ESP_ERR_INVALID_ARG;
  profile_t *p = malloc(sizeof(*p));
  if (!p)
    return ESP_ERR_NO_MEM;
  if (app_data_recovery_required()) {
    uint8_t target[32];
    if (target_read(target) || memcmp(id, target, 32)) {
      free(p);
      snprintf(m, n,
               "Retome primeiro o aplicativo da troca de dados interrompida.");
      return ESP_ERR_INVALID_STATE;
    }
  }
  esp_err_t loaded = app_data_recovery_required()
                         ? frozen_profile(id, false, p, NULL)
                         : ESP_ERR_NOT_FOUND;
  if (loaded == ESP_ERR_NOT_FOUND) {
    if (app_data_recovery_required()) {
      free(p);
      return ESP_ERR_INVALID_STATE;
    }
    loaded = load(id, p);
  }
  if (loaded != ESP_OK && loaded != ESP_ERR_NOT_FOUND) {
    free(p);
    return loaded;
  }
  bool configured = loaded == ESP_OK;
  esp_err_t e = ESP_OK;
  if (!configured) {
    if (app_data_recovery_required()) {
      free(p);
      return ESP_ERR_INVALID_STATE;
    }
    e = app_data_save_active(cb, u, m, n);
    if (!e) {
      owner_t old;
      if (owner_read(&old) == ESP_OK && old.active)
        e = app_data_release_owner();
    }
    free(p);
    return e;
  }
  if (!app_data_recovery_required())
    e = app_data_save_active(cb, u, m, n);
  if (!e && !app_data_recovery_required()) {
    app_data_config_t baseline = p->config;
    uint8_t material[33];
    material[0] = 0xb1;
    memcpy(material + 1, id, 32);
    mbedtls_sha256(material, sizeof(material), baseline.identity, 0);
    e = snapshot(&baseline, false, cb, u, m, n);
  }
  if (!e && !app_data_recovery_required()) {
    owner_t previous = {.schema = 1};
    size_t bytes = sizeof(previous);
    esp_err_t read = launcher_store_read("profile-owner", &previous, &bytes);
    if (read == ESP_ERR_NOT_FOUND)
      previous = (owner_t){.schema = 1};
    else if (read || bytes != sizeof(previous))
      e = ESP_ERR_INVALID_CRC;
    if (!e)
      e = launcher_store_write("profile-before-owner", &previous,
                               sizeof(previous));
  }
  uint32_t state = 1;
  if (!e)
    e = launcher_store_write("profile-target", id, 32);
  if (!e)
    e = marker(true, id, p);
  if (!e)
    e = launcher_store_write("profile-transaction", &state, sizeof(state));
  if (!e)
    e = restore_profile(p, cb, u);
  if (!e) {
    char key[48];
    key_for(id, key);
    e = launcher_store_write(key, p, sizeof(*p));
  }
  if (!e) {
    owner_t owner = {.schema = 1, .active = 1};
    memcpy(owner.identity, id, 32);
    e = owner_write(&owner);
  }
  if (!e) {
    state = 0;
    e = launcher_store_write("profile-transaction", &state, sizeof(state));
    if (!e)
      e = marker(false, NULL, NULL);
  }
  snprintf(m, n, "%s",
           e ? "Troca de perfil interrompida. Tente iniciar o mesmo app para "
               "retomar."
             : "Dados do aplicativo restaurados e verificados");
  free(p);
  return e;
}
esp_err_t app_data_capture_return(app_data_progress_fn cb, void *u, char *m,
                                  size_t n) {
  if (app_data_recovery_required())
    return app_data_recover(false, cb, u, m, n);
  return app_data_save_active(cb, u, m, n);
}

esp_err_t app_data_recover(bool rollback, app_data_progress_fn cb, void *u,
                           char *m, size_t n) {
  if (!app_data_recovery_required())
    return ESP_OK;
  uint8_t id[32];
  size_t bytes = 32;
  esp_err_t e = target_read(id);
  if (e || bytes != 32)
    return ESP_ERR_INVALID_CRC;
  uint8_t target_id[32];
  memcpy(target_id, id, 32);
  if (rollback) {
    uint8_t material[33];
    material[0] = 0xb1;
    memcpy(material + 1, id, 32);
    mbedtls_sha256(material, sizeof(material), id, 0);
  }
  profile_t *p = malloc(sizeof(*p));
  if (!p)
    return ESP_ERR_NO_MEM;
  owner_t frozen_previous;
  bool has_frozen = true;
  e = frozen_profile(target_id, rollback, p, &frozen_previous);
  if (e == ESP_ERR_NOT_FOUND) {
    has_frozen = false;
    e = ESP_ERR_INVALID_STATE;
  }
  if (!e)
    e = restore_profile(p, cb, u);
  if (!e && !rollback) {
    char key[48];
    key_for(id, key);
    e = launcher_store_write(key, p, sizeof(*p));
  }
  free(p);
  owner_t owner = {.schema = 1, .active = 1};
  memcpy(owner.identity, id, 32);
  if (!e && rollback && has_frozen)
    owner = frozen_previous;
  if (!e && rollback && !has_frozen) {
    bytes = sizeof(owner);
    e = launcher_store_read("profile-before-owner", &owner, &bytes);
    if (!e && bytes != sizeof(owner))
      e = ESP_ERR_INVALID_CRC;
  }
  if (!e)
    e = owner_write(&owner);
  if (!e) {
    uint32_t state = 0;
    e = launcher_store_write("profile-transaction", &state, sizeof(state));
    if (!e)
      e = marker(false, NULL, NULL);
  }
  snprintf(m, n, "%s",
           e ? "Não foi possível recuperar o perfil; snapshots preservados"
           : rollback ? "Dados anteriores à troca restaurados"
                      : "Troca de perfil retomada e verificada");
  return e;
}
esp_err_t app_data_inherit(const uint8_t old[32], const uint8_t id[32],
                           app_data_progress_fn cb, void *u, char *m,
                           size_t n) {
  app_data_config_t *c = malloc(sizeof(*c));
  if (!c)
    return ESP_ERR_NO_MEM;
  if (!app_data_get(old, c)) {
    free(c);
    return ESP_OK;
  }
  memcpy(c->identity, id, 32);
  esp_err_t e = snapshot(c, false, cb, u, m, n);
  free(c);
  return e;
}

esp_err_t app_data_release_owner(void) {
  owner_t owner = {.schema = 1};
  return owner_write(&owner);
}
