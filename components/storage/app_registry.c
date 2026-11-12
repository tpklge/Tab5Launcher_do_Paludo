#include "storage_guard.h"
/* Image identity survives slot moves, source renames and SD removal. */
#include "app_registry.h"
#include "launcher_store.h"
#include "sd_files.h"
#include <stdio.h>
#include <string.h>
typedef struct {
  uint32_t schema;
  uint8_t identity[32];
  app_metadata_t metadata;
} record_t;
static void key_for(const uint8_t id[32], char key[15]) {
  for (int i = 0; i < 7; i++)
    snprintf(key + 2 * i, 3, "%02x", id[i]);
}
bool app_registry_get(const uint8_t id[32], app_metadata_t *out) {
  memset(out, 0, sizeof(*out));
  char key[15];
  key_for(id, key);
  record_t record;
  size_t n = sizeof(record);
  esp_err_t err = launcher_store_read(key, &record, &n);
  if (err != ESP_OK || n != sizeof(record) || record.schema != 1 ||
      memcmp(record.identity, id, 32) ||
      !memchr(record.metadata.name, 0, sizeof(record.metadata.name)) ||
      !memchr(record.metadata.icon, 0, sizeof(record.metadata.icon)))
    return false;
  if (record.metadata.icon[0] &&
      strncmp(record.metadata.icon, LAUNCHER_ROOT "/",
              strlen(LAUNCHER_ROOT) + 1)) {
    char managed[512];
    if (!launcher_adopt_image(record.metadata.icon, "icons", managed,
                              sizeof(managed))) {
      snprintf(record.metadata.icon, sizeof(record.metadata.icon), "%s",
               managed);
      launcher_store_write(key, &record, sizeof(record));
    }
  }
  *out = record.metadata;
  return true;
}
esp_err_t app_registry_set(const uint8_t id[32],
                           const app_metadata_t *metadata) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!id || !metadata || !memchr(metadata->name, 0, sizeof(metadata->name)) ||
      !memchr(metadata->icon, 0, sizeof(metadata->icon)) ||
      (metadata->icon[0] && !sd_path_valid(metadata->icon)))
    return ESP_ERR_INVALID_ARG;
  for (const unsigned char *p = (const unsigned char *)metadata->name; *p; p++)
    if (*p < 32)
      return ESP_ERR_INVALID_ARG;
  char key[15];
  key_for(id, key);
  record_t record = {.schema = 1, .metadata = *metadata};
  if (metadata->icon[0]) {
    esp_err_t e =
        launcher_adopt_image(metadata->icon, "icons", record.metadata.icon,
                             sizeof(record.metadata.icon));
    if (e)
      return e;
  }
  memcpy(record.identity, id, 32);
  record_t old;
  size_t n = sizeof(old);
  esp_err_t prior = launcher_store_read(key, &old, &n);
  if (prior == ESP_OK && (n != sizeof(old) || memcmp(old.identity, id, 32)))
    return ESP_ERR_INVALID_STATE;
  return launcher_store_write(key, &record, sizeof(record));
}
