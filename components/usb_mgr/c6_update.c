#include "cJSON.h"
#include "esp_hosted_ota.h"
#include "firmware_parser.h"
#include "installer.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include "usb.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static bool read_image(void *f, uint32_t off, void *out, size_t n) {
  return !fseek(f, off, SEEK_SET) && fread(out, 1, n, f) == n;
}
static bool hash_image(void *f, uint32_t size, const uint8_t expected[32]) {
  if (fseek(f, 0, SEEK_SET))
    return false;
  uint8_t b[1024], actual[32];
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  bool ok = true;
  for (uint32_t off = 0; off < size;) {
    unsigned n = size - off < sizeof(b) ? size - off : sizeof(b);
    if (fread(b, 1, n, f) != n) {
      ok = false;
      break;
    }
    mbedtls_sha256_update(&h, b, n);
    off += n;
  }
  mbedtls_sha256_finish(&h, actual);
  mbedtls_sha256_free(&h);
  return ok && !memcmp(actual, expected, 32);
}
static esp_err_t update(void *arg) {
  const char *expected = arg;
  if (!expected || strlen(expected) != 64)
    return ESP_ERR_INVALID_ARG;
  FILE *meta = fopen(LAUNCHER_ROOT "/c6/manifest.json", "rb");
  if (!meta)
    return ESP_ERR_NOT_FOUND;
  char text[2048];
  size_t n = fread(text, 1, sizeof(text) - 1, meta);
  bool ok = !ferror(meta) && fgetc(meta) == EOF;
  fclose(meta);
  if (!ok)
    return ESP_ERR_INVALID_SIZE;
  text[n] = 0;
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true),
        *board = cJSON_GetObjectItemCaseSensitive(j, "board"),
        *transport = cJSON_GetObjectItemCaseSensitive(j, "transport"),
        *sha = cJSON_GetObjectItemCaseSensitive(j, "sha256");
  ok = cJSON_IsString(board) && !strcmp(board->valuestring, "M5Stack Tab5") &&
       cJSON_IsString(transport) && !strcmp(transport->valuestring, "SDIO") &&
       cJSON_IsString(sha) && !strcmp(sha->valuestring, expected);
  cJSON_Delete(j);
  if (!ok)
    return ESP_ERR_INVALID_ARG;
  FILE *f = fopen(LAUNCHER_ROOT "/c6/network_adapter.bin", "rb");
  if (!f)
    return ESP_ERR_NOT_FOUND;
  struct stat st;
  firmware_image_t image;
  char error[160];
  if (fstat(fileno(f), &st) || st.st_size <= 0 || st.st_size > 1920 * 1024 ||
      !firmware_parse_chip(f, read_image, hash_image, st.st_size, 0x0d,
                           UINT16_MAX, &image, error, sizeof(error)) ||
      strcmp(image.project, "network_adapter") ||
      strcmp(image.version, "2.13.1") || !image.hash_verified) {
    fclose(f);
    return ESP_ERR_NOT_SUPPORTED;
  }
  uint8_t digest[32];
  for (unsigned i = 0; i < 32; i++) {
    char pair[3] = {expected[2 * i], expected[2 * i + 1], 0}, *end;
    unsigned long v = strtoul(pair, &end, 16);
    if (*end) {
      fclose(f);
      return ESP_ERR_INVALID_ARG;
    }
    digest[i] = v;
  }
  if (!hash_image(f, st.st_size, digest)) {
    fclose(f);
    return ESP_ERR_INVALID_CRC;
  }
  /* All validation precedes OTA begin. Keep old active image until end
   * succeeds. */
  esp_err_t e = wifi_pause_wait(15000);
  uint8_t *buf = malloc(4096);
  if (!buf && !e) e = ESP_ERR_NO_MEM;
  if (!e) e = esp_hosted_slave_ota_begin();
  fseek(f, 0, SEEK_SET);
  for (uint32_t off = 0; !e && off < (uint32_t)st.st_size;) {
    unsigned count = st.st_size - off < 4096 ? st.st_size - off : 4096;
    if (fread(buf, 1, count, f) != count)
      e = ESP_FAIL;
    else
      e = esp_hosted_slave_ota_write(buf, count);
    off += count;
  }
  free(buf);
  fclose(f);
  if (!e)
    e = esp_hosted_slave_ota_end();
  if (!e)
    e = esp_hosted_slave_ota_activate();
  if (e)
    wifi_resume();
  return e;
}
esp_err_t usb_update_c6(const char *sha) {
  return installer_maintenance(update, (void *)sha);
}
