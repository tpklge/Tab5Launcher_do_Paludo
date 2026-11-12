#include "storage_guard.h"
#include "cJSON.h"
#include "catalog.h"
#include "json_limits.h"
#include "launcher_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
esp_err_t catalog_fetch_sources(catalog_list_t *out, char (*origins)[512],
                                bool refresh, char *message, size_t size,
                                catalog_progress_fn cb, void *u) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  if (!out || !origins)
    return ESP_ERR_INVALID_ARG;
  FILE *f = fopen(LAUNCHER_ROOT "/ota_sources.json", "rb");
  if (!f) {
    snprintf(message, size,
             "Edite /launcher/ota_sources.json no cartao para definir as "
             "fontes OTA.");
    return ESP_ERR_NOT_FOUND;
  }
  char *text = malloc(16385);
  if (!text) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  size_t n = fread(text, 1, 16384, f);
  bool valid = !ferror(f) && fgetc(f) == EOF;
  fclose(f);
  text[n] = 0;
  cJSON *j = valid && json_depth_valid(text, 5)
                 ? cJSON_ParseWithOpts(text, NULL, true)
                 : NULL;
  free(text);
  cJSON *sources = cJSON_GetObjectItemCaseSensitive(j, "sources");
  unsigned count = cJSON_GetArraySize(sources);
  if (!cJSON_IsObject(j) || !cJSON_IsArray(sources) || !count || count > 8) {
    cJSON_Delete(j);
    snprintf(message, size,
             "ota_sources.json invalido: use sources com 1 a 8 URLs HTTPS.");
    return ESP_ERR_INVALID_ARG;
  }
  for (unsigned i = 0; i < count; i++) {
    cJSON *entry = cJSON_GetArrayItem(sources, i),
          *url = cJSON_GetObjectItemCaseSensitive(entry, "url");
    if (!cJSON_IsString(url) || !catalog_https_url(url->valuestring)) {
      cJSON_Delete(j);
      snprintf(message, size, "URL HTTPS invalida no arquivo de fontes OTA.");
      return ESP_ERR_INVALID_ARG;
    }
  }
  catalog_list_t *part = calloc(1, sizeof(*part));
  if (!part) {
    cJSON_Delete(j);
    return ESP_ERR_NO_MEM;
  }
  out->count = 0;
  unsigned failed = 0;
  esp_err_t last = ESP_FAIL;
  char error[300] = {0};
  for (unsigned i = 0; i < count; i++) {
    const char *url =
        cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(sources, i), "url")
            ->valuestring;
    memset(part, 0, sizeof(*part));
    last = (refresh ? catalog_fetch_refresh : catalog_fetch_progress)(
        url, part, error, sizeof(error), cb, u);
    if (last == ESP_ERR_INVALID_STATE) {
      free(part);
      cJSON_Delete(j);
      snprintf(message, size, "Consulta cancelada");
      return last;
    }
    if (last) {
      failed++;
      continue;
    }
    unsigned limit = CATALOG_MAX_ITEMS / count;
    if (limit < 1)
      limit = 1;
    for (unsigned k = 0;
         k < part->count && k < limit && out->count < CATALOG_MAX_ITEMS; k++) {
      unsigned at = out->count++;
      out->items[at] = part->items[k];
      snprintf(origins[at], 512, "%s", url);
    }
  }
  free(part);
  cJSON_Delete(j);
  snprintf(message, size, "%u apps / %u fontes; %u falha(s). %s", out->count,
           count, failed, failed ? error : "");
  return out->count ? ESP_OK : last;
}
