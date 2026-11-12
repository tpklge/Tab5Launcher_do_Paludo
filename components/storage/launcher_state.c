#include "launcher_state.h"
#include "launcher_store.h"
#include "storage_guard.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ENTRIES 24
#define VALUE_BYTES 600
#define STATE_MAGIC 0x3154534c
/* Fixed-size bounded encoding inside the CRC-checked, validated SD record. */
typedef struct {
  char key[24];
  uint16_t size, type;
  uint8_t bytes[VALUE_BYTES];
} entry_t;
typedef struct {
  uint32_t magic;
  entry_t entries[ENTRIES];
} record_t;
struct launcher_state {
  record_t record;
  char name[49];
  bool writable, lease;
};
static bool name_valid(const char *s, size_t limit) {
  if (!s || !*s || strlen(s) >= limit)
    return false;
  for (; *s; s++)
    if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-')
      return false;
  return true;
}
static bool valid(const record_t *r) {
  if (r->magic != STATE_MAGIC)
    return false;
  for (unsigned i = 0; i < ENTRIES; i++) {
    const entry_t *e = &r->entries[i];
    if (!e->key[0])
      continue;
    if (!memchr(e->key, 0, sizeof(e->key)) ||
        !name_valid(e->key, sizeof(e->key)) || e->size > VALUE_BYTES ||
        e->type < 1 || e->type > 4)
      return false;
    if ((e->type == 3 || e->type == 4) && e->size != 1)
      return false;
    if (e->type == 2 &&
        (!e->size || e->bytes[e->size - 1] || memchr(e->bytes, 0, e->size - 1)))
      return false;
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(e->key, r->entries[j].key))
        return false;
  }
  return true;
}
esp_err_t launcher_state_open(const char *name, int mode,
                              launcher_state_handle_t *out) {
  if (!out || !name_valid(name, 40) ||
      (mode != LAUNCHER_STATE_READONLY && mode != LAUNCHER_STATE_READWRITE))
    return ESP_ERR_INVALID_ARG;
  *out = NULL;
  bool writable = mode == LAUNCHER_STATE_READWRITE;
  if (!(writable ? storage_write_acquire() : storage_io_acquire()))
    return ESP_ERR_INVALID_STATE;
  launcher_state_handle_t h = calloc(1, sizeof(*h));
  if (!h) {
    if (writable)
      storage_write_release();
    else
      storage_io_release();
    return ESP_ERR_NO_MEM;
  }
  h->writable = writable;
  h->lease = true;
  snprintf(h->name, sizeof(h->name), "state-%s", name);
  size_t size = sizeof(h->record);
  esp_err_t e = launcher_store_read(h->name, &h->record, &size);
  if (e == ESP_ERR_NOT_FOUND && writable) {
    memset(&h->record, 0, sizeof(h->record));
    h->record.magic = STATE_MAGIC;
    e = ESP_OK;
  } else if (!e && (size != sizeof(h->record) || !valid(&h->record)))
    e = ESP_ERR_INVALID_CRC;
  if (e) {
    launcher_state_close(h);
    return e;
  }
  *out = h;
  return ESP_OK;
}
void launcher_state_close(launcher_state_handle_t h) {
  if (!h)
    return;
  if (h->lease) {
    if (h->writable)
      storage_write_release();
    else
      storage_io_release();
  }
  memset(h, 0, sizeof(*h));
  free(h);
}
esp_err_t launcher_state_commit(launcher_state_handle_t h) {
  if (!h || !h->writable || !valid(&h->record))
    return ESP_ERR_INVALID_STATE;
  return launcher_store_write(h->name, &h->record, sizeof(h->record));
}
static entry_t *find(launcher_state_handle_t h, const char *key) {
  if (!h || !name_valid(key, 24))
    return NULL;
  for (unsigned i = 0; i < ENTRIES; i++)
    if (!strcmp(h->record.entries[i].key, key))
      return &h->record.entries[i];
  return NULL;
}
static esp_err_t get(launcher_state_handle_t h, const char *key, void *out,
                     size_t *size, unsigned type) {
  if (!h || !size || !name_valid(key, 24))
    return ESP_ERR_INVALID_ARG;
  entry_t *e = find(h, key);
  if (!e)
    return ESP_ERR_NOT_FOUND;
  if (e->type != type)
    return ESP_ERR_INVALID_STATE;
  if (!out) {
    *size = e->size;
    return ESP_OK;
  }
  if (*size < e->size) {
    *size = e->size;
    return ESP_ERR_INVALID_SIZE;
  }
  *size = e->size;
  memcpy(out, e->bytes, e->size);
  return ESP_OK;
}
static esp_err_t put(launcher_state_handle_t h, const char *key,
                     const void *data, size_t size, unsigned type) {
  if (!h || !h->writable)
    return ESP_ERR_INVALID_STATE;
  if (!name_valid(key, 24) || (!data && size) || size > VALUE_BYTES)
    return ESP_ERR_INVALID_ARG;
  entry_t *e = find(h, key);
  if (!e)
    for (unsigned i = 0; i < ENTRIES; i++)
      if (!h->record.entries[i].key[0]) {
        e = &h->record.entries[i];
        break;
      }
  if (!e)
    return ESP_ERR_NO_MEM;
  memset(e, 0, sizeof(*e));
  snprintf(e->key, sizeof(e->key), "%s", key);
  e->size = size;
  e->type = type;
  if (size)
    memcpy(e->bytes, data, size);
  return ESP_OK;
}
esp_err_t launcher_state_erase_key(launcher_state_handle_t h, const char *key) {
  if (!h || !h->writable)
    return ESP_ERR_INVALID_STATE;
  if (!name_valid(key, 24))
    return ESP_ERR_INVALID_ARG;
  entry_t *e = find(h, key);
  if (!e)
    return ESP_ERR_NOT_FOUND;
  memset(e, 0, sizeof(*e));
  return ESP_OK;
}
esp_err_t launcher_state_get_blob(launcher_state_handle_t h, const char *k,
                                  void *v, size_t *n) {
  return get(h, k, v, n, 1);
}
esp_err_t launcher_state_set_blob(launcher_state_handle_t h, const char *k,
                                  const void *v, size_t n) {
  return put(h, k, v, n, 1);
}
esp_err_t launcher_state_get_str(launcher_state_handle_t h, const char *k,
                                 char *v, size_t *n) {
  return get(h, k, v, n, 2);
}
esp_err_t launcher_state_set_str(launcher_state_handle_t h, const char *k,
                                 const char *v) {
  return v ? put(h, k, v, strlen(v) + 1, 2) : ESP_ERR_INVALID_ARG;
}
esp_err_t launcher_state_get_u8(launcher_state_handle_t h, const char *k,
                                uint8_t *v) {
  size_t n = 1;
  return v ? get(h, k, v, &n, 3) : ESP_ERR_INVALID_ARG;
}
esp_err_t launcher_state_set_u8(launcher_state_handle_t h, const char *k,
                                uint8_t v) {
  return put(h, k, &v, 1, 3);
}
esp_err_t launcher_state_get_i8(launcher_state_handle_t h, const char *k,
                                int8_t *v) {
  size_t n = 1;
  return v ? get(h, k, v, &n, 4) : ESP_ERR_INVALID_ARG;
}
esp_err_t launcher_state_set_i8(launcher_state_handle_t h, const char *k,
                                int8_t v) {
  return put(h, k, &v, 1, 4);
}
