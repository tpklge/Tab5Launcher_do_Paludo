/* Bounded, versioned SD credential record. Fields are published together. */
#include "wifi_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "launcher_store.h"
#include <stdio.h>
#include <string.h>

#define STORE_MAGIC 0x31574654u

typedef struct {
  uint32_t magic;
  uint8_t count;
  uint8_t reserved[3];
  wifi_saved_network_t
      networks[WIFI_MAX_SAVED]; /* most recently connected first */
} saved_blob_t;
static SemaphoreHandle_t s_lock;
static const char *TAG = "wifi_store";
static saved_blob_t s_cache;
static bool s_loaded;

esp_err_t wifi_saved_init(void) {
  s_loaded = false;
  if (!s_lock)
    s_lock = xSemaphoreCreateMutex();
  return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}
static bool valid(const wifi_saved_network_t *n) {
  return n->ssid[0] && memchr(n->ssid, 0, sizeof(n->ssid)) &&
         memchr(n->password, 0, sizeof(n->password));
}
static esp_err_t load(saved_blob_t *blob) {
  if (s_loaded) {
    *blob = s_cache;
    return ESP_OK;
  }
  size_t n = sizeof(*blob);
  esp_err_t e = launcher_store_read("wifi", blob, &n);
  if (e == ESP_ERR_NOT_FOUND) {
    memset(blob, 0, sizeof(*blob));
    blob->magic = STORE_MAGIC;
    e = ESP_OK;
  }
  if (e == ESP_OK && (n != sizeof(*blob) || blob->magic != STORE_MAGIC ||
                      blob->count > WIFI_MAX_SAVED))
    e = ESP_ERR_INVALID_STATE;
  for (unsigned i = 0; e == ESP_OK && i < blob->count; i++)
    if (!valid(&blob->networks[i]))
      e = ESP_ERR_INVALID_STATE;
  if (e == ESP_OK) {
    s_cache = *blob;
    s_loaded = true;
  }
  return e;
}
static esp_err_t save(const saved_blob_t *blob) {
  esp_err_t e = launcher_store_write("wifi", blob, sizeof(*blob));
  if (!e) {
    s_cache = *blob;
    s_loaded = true;
  }
  return e;
}
esp_err_t wifi_store_put(const wifi_saved_network_t *network) {
  if (!network || !valid(network))
    return ESP_ERR_INVALID_ARG;
  if (!s_lock)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  esp_err_t err = load(&blob);
  if (err == ESP_OK) {
    unsigned index = blob.count;
    for (unsigned i = 0; i < blob.count; ++i)
      if (strcmp(blob.networks[i].ssid, network->ssid) == 0) {
        index = i;
        break;
      }
    if (index == blob.count && blob.count < WIFI_MAX_SAVED)
      blob.count++;
    if (index >= WIFI_MAX_SAVED)
      index = WIFI_MAX_SAVED - 1;
    memmove(&blob.networks[1], &blob.networks[0], index * sizeof(*network));
    blob.networks[0] = *network;
    err = save(&blob);
  }
  xSemaphoreGive(s_lock);
  return err;
}
bool wifi_store_latest(wifi_saved_network_t *network) {
  if (!s_lock || !network)
    return false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  bool found = load(&blob) == ESP_OK && blob.count > 0;
  if (found)
    *network = blob.networks[0];
  xSemaphoreGive(s_lock);
  return found;
}
int wifi_saved_count(void) {
  if (!s_lock)
    return 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  int count = load(&blob) == ESP_OK ? blob.count : 0;
  xSemaphoreGive(s_lock);
  return count;
}
int wifi_saved_ssids(char (*ssids)[33], int max) {
  if (!s_lock || !ssids || max <= 0)
    return 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  int count = load(&blob) == ESP_OK ? blob.count : 0;
  if (count > max)
    count = max;
  for (int i = 0; i < count; i++)
    memcpy(ssids[i], blob.networks[i].ssid, 33);
  xSemaphoreGive(s_lock);
  return count;
}
bool wifi_saved_password(const char *ssid, char *pass, size_t len) {
  if (!s_lock || !ssid || !pass || !len)
    return false;
  pass[0] = 0;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  bool found = false;
  if (load(&blob) == ESP_OK) {
    for (unsigned i = 0; i < blob.count; i++) {
      if (strcmp(ssid, blob.networks[i].ssid) != 0)
        continue;
      size_t size = strlen(blob.networks[i].password) + 1;
      if (size <= len) {
        memcpy(pass, blob.networks[i].password, size);
        found = true;
      }
      break;
    }
  }
  xSemaphoreGive(s_lock);
  return found;
}
void wifi_forget(const char *ssid) {
  if (!s_lock || !ssid)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob;
  esp_err_t err = load(&blob);
  if (err == ESP_OK) {
    for (unsigned i = 0; i < blob.count; i++) {
      if (strcmp(ssid, blob.networks[i].ssid) != 0)
        continue;
      memmove(&blob.networks[i], &blob.networks[i + 1],
              (blob.count - i - 1) * sizeof(blob.networks[0]));
      memset(&blob.networks[--blob.count], 0, sizeof(blob.networks[0]));
      err = save(&blob);
      break;
    }
  }
  xSemaphoreGive(s_lock);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "forget failed: %s", esp_err_to_name(err));
}
void wifi_forget_all(void) {
  if (!s_lock)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  saved_blob_t blob = {.magic = STORE_MAGIC};
  esp_err_t err = save(&blob); /* empty blob overrides legacy keys */
  xSemaphoreGive(s_lock);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "forget all failed: %s", esp_err_to_name(err));
}
