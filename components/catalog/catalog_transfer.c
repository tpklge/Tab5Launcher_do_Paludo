#include "storage_guard.h"
#include "catalog_transfer.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define MAXIMUM (32u * 1024 * 1024)
typedef struct {
  char etag[160], range[128];
  bool overflow;
} response_t;
typedef struct {
  uint32_t schema, total;
  char url[512], etag[160];
  uint8_t digest[32];
  uint32_t complete;
} resume_t;
static esp_err_t event(esp_http_client_event_t *e) {
  response_t *r = e->user_data;
  if (!r)
    return ESP_OK;
  if (e->event_id == HTTP_EVENT_ON_HEADER) {
    char *out = NULL;
    size_t size = 0;
    if (!strcasecmp(e->header_key, "ETag")) {
      out = r->etag;
      size = sizeof(r->etag);
    } else if (!strcasecmp(e->header_key, "Content-Range")) {
      out = r->range;
      size = sizeof(r->range);
    }
    if (out) {
      if (strlen(e->header_value) >= size)
        r->overflow = true;
      else
        snprintf(out, size, "%s", e->header_value);
    }
  }
  return ESP_OK;
}
static bool strong_etag(const char *tag) {
  size_t n = strlen(tag);
  if (n < 2 || tag[0] != '"' || tag[n - 1] != '"')
    return false;
  for (size_t i = 1; i + 1 < n; i++)
    if ((unsigned char)tag[i] < 0x21 || tag[i] == '"' ||
        (unsigned char)tag[i] == 0x7f)
      return false;
  return true;
}
static void state_key(const char *path, char key[48]) {
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t *)path, strlen(path), hash, 0);
  snprintf(key, 48, "resume-%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6],
           hash[7], hash[8], hash[9], hash[10], hash[11]);
}
static bool hash_path(const char *path, uint8_t digest[32], uint32_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  uint8_t *buf = malloc(4096);
  if (!buf) {
    fclose(f);
    return false;
  }
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  size_t n;
  uint64_t size = 0;
  while ((n = fread(buf, 1, 4096, f))) {
    size += n;
    if (size > MAXIMUM)
      break;
    mbedtls_sha256_update(&h, buf, n);
  }
  bool ok = !ferror(f) && size <= MAXIMUM;
  mbedtls_sha256_finish(&h, digest);
  mbedtls_sha256_free(&h);
  free(buf);
  fclose(f);
  if (length)
    *length = size;
  return ok;
}
static bool expected(const catalog_part_t *p, const uint8_t *d) {
  if (!p->sha256[0])
    return true;
  char hex[65];
  for (unsigned i = 0; i < 32; i++)
    snprintf(hex + i * 2, 3, "%02x", d[i]);
  return !strcasecmp(hex, p->sha256);
}
static esp_http_client_handle_t open_part(const catalog_part_t *p,
                                          uint32_t offset, const char *etag,
                                          response_t *r) {
  esp_http_client_config_t config = {.url = p->url,
                                     .timeout_ms = 15000,
                                     .crt_bundle_attach = esp_crt_bundle_attach,
                                     .disable_auto_redirect = true,
                                     .buffer_size = 16384,
                                     .buffer_size_tx = 1024,
                                     .event_handler = event,
                                     .user_data = r};
  esp_http_client_handle_t h = esp_http_client_init(&config);
  if (!h)
    return NULL;
  esp_http_client_set_header(h, "Accept-Encoding", "identity");
  if (offset) {
    char range[48];
    snprintf(range, sizeof(range), "bytes=%lu-", (unsigned long)offset);
    esp_http_client_set_header(h, "Range", range);
    if (strong_etag(etag))
      esp_http_client_set_header(h, "If-Range", etag);
  }
  for (unsigned i = 0; i < 6; i++) {
    memset(r, 0, sizeof(*r));
    if (esp_http_client_open(h, 0) || esp_http_client_fetch_headers(h) < 0)
      break;
    int code = esp_http_client_get_status_code(h);
    if (code == 200 || code == 206)
      return h;
    if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308)
      break;
    if (i == 5 || esp_http_client_set_redirection(h))
      break;
    char url[512];
    if (esp_http_client_get_url(h, url, sizeof(url)) || !catalog_https_url(url))
      break;
    esp_http_client_close(h);
  }
  esp_http_client_cleanup(h);
  return NULL;
}
esp_err_t catalog_transfer(const catalog_part_t *p, const char *path,
                           catalog_progress_fn cb, void *u) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!p || !path || !catalog_https_url(p->url) || time(NULL) < 1704067200 ||
      strlen(path) > 480 || p->size > MAXIMUM)
    return ESP_ERR_INVALID_ARG;
  char key[48], partial[512];
  state_key(path, key);
  snprintf(partial, sizeof(partial), "%s.part", path);
  resume_t state = {0};
  size_t size = sizeof(state);
  bool known = launcher_store_read(key, &state, &size) == ESP_OK &&
               size == sizeof(state) && state.schema == 1 &&
               memchr(state.url, 0, sizeof(state.url)) &&
               memchr(state.etag, 0, sizeof(state.etag)) &&
               !strcmp(state.url, p->url) && state.total <= MAXIMUM;
  uint8_t digest[32];
  uint32_t actual = 0;
  struct stat st;
  if (!stat(path, &st)) {
    if (known && state.complete && hash_path(path, digest, &actual) &&
        actual == state.total && !memcmp(digest, state.digest, 32) &&
        expected(p, digest))
      return ESP_OK;
    return ESP_ERR_INVALID_STATE;
  }
  uint32_t offset =
      (!stat(partial, &st) && st.st_size > 0 && st.st_size <= MAXIMUM)
          ? st.st_size
          : 0;
  if (!known || (!p->sha256[0] && !strong_etag(state.etag)))
    offset = 0;
  if (offset && state.total && offset == state.total &&
      hash_path(partial, digest, &actual) && actual == offset &&
      expected(p, digest) &&
      (p->sha256[0] || (state.complete && !memcmp(state.digest, digest, 32)))) {
    state.complete = 1;
    memcpy(state.digest, digest, 32);
    if (launcher_store_write(key, &state, sizeof(state)))
      return ESP_FAIL;
    return rename(partial, path) ? ESP_FAIL : ESP_OK;
  }
  if (offset && state.total && offset >= state.total)
    offset = 0;
  response_t response;
  esp_http_client_handle_t h =
      open_part(p, offset, offset ? state.etag : "", &response);
  if (!h)
    return ESP_FAIL;
  int code = esp_http_client_get_status_code(h);
  uint32_t total = 0;
  int64_t announced = esp_http_client_get_content_length(h);
  if (response.overflow) {
    esp_http_client_cleanup(h);
    return ESP_ERR_INVALID_SIZE;
  }
  if (code == 206) {
    unsigned long start, end, length;
    int endpos = 0;
    bool valid = offset &&
                 sscanf(response.range, "bytes %lu-%lu/%lu%n", &start, &end,
                        &length, &endpos) == 3 &&
                 !response.range[endpos] && start == offset && end >= start &&
                 length > 0 && length <= MAXIMUM && end < length &&
                 (!p->size || p->size == length) &&
                 (announced < 0 || (uint64_t)announced == end - start + 1);
    if (!p->sha256[0] && strcmp(response.etag, state.etag))
      valid = false;
    if (!valid) {
      esp_http_client_cleanup(h);
      return ESP_ERR_INVALID_CRC;
    }
    total = length;
  } else {
    offset = 0;
    if (announced > MAXIMUM) {
      esp_http_client_cleanup(h);
      return ESP_ERR_INVALID_SIZE;
    }
    total = p->size ? p->size : (announced > 0 ? announced : 0);
    if (p->size && announced > 0 && (uint64_t)announced != p->size) {
      esp_http_client_cleanup(h);
      return ESP_ERR_INVALID_SIZE;
    }
  }
  if (offset && total < offset) {
    esp_http_client_cleanup(h);
    return ESP_ERR_INVALID_SIZE;
  }
  state = (resume_t){.schema = 1, .total = total};
  snprintf(state.url, sizeof(state.url), "%s", p->url);
  snprintf(state.etag, sizeof(state.etag), "%s", response.etag);
  esp_err_t err = launcher_store_write(key, &state, sizeof(state));
  uint8_t *buf = malloc(4096);
  if (!buf)
    err = ESP_ERR_NO_MEM;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  FILE *f = NULL;
  if (!err && offset) {
    f = fopen(partial, "rb");
    if (!f)
      err = ESP_FAIL;
    else {
      uint32_t left = offset;
      while (left && !err) {
        unsigned n = left > 4096 ? 4096 : left;
        if (fread(buf, 1, n, f) != n) {
          err = ESP_FAIL;
          break;
        }
        mbedtls_sha256_update(&sha, buf, n);
        left -= n;
      }
      fclose(f);
      f = NULL;
    }
  }
  if (!err) {
    f = fopen(partial, offset ? "ab" : "wb");
    if (!f)
      err = ESP_FAIL;
  }
  uint32_t used = offset;
  unsigned retries = 0;
  int last = 0;
  while (!err && used < (total ? total : MAXIMUM)) {
    unsigned need = (total ? total : MAXIMUM) - used;
    if (need > 4096)
      need = 4096;
    last = esp_http_client_read(h, (char *)buf, need);
    if (last == -ESP_ERR_HTTP_EAGAIN && retries++ < 2)
      continue;
    if (last < 0) {
      err = ESP_FAIL;
      break;
    }
    if (!last)
      break;
    retries = 0;
    if (fwrite(buf, 1, last, f) != (size_t)last) {
      err = ESP_FAIL;
      break;
    }
    mbedtls_sha256_update(&sha, buf, last);
    used += last;
    if (cb && !cb(total ? (uint64_t)used * 100 / total : 0,
                  offset ? "Retomando download" : "Baixando firmware", u))
      err = ESP_ERR_INVALID_STATE;
  }
  if (!err) {
    char extra;
    last = esp_http_client_read(h, &extra, 1);
    if (last != 0 || !esp_http_client_is_complete_data_received(h) || !used ||
        (total && used != total))
      err = ESP_ERR_INVALID_SIZE;
  }
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  if (f) {
    if (fflush(f) || fsync(fileno(f)))
      err = ESP_FAIL;
    if (fclose(f))
      err = ESP_FAIL;
  }
  esp_http_client_cleanup(h);
  free(buf);
  if (!err && !expected(p, digest)) {
    unlink(partial);
    err = ESP_ERR_INVALID_CRC;
  }
  if (!err) {
    uint8_t reread[32];
    uint32_t bytes;
    if (!hash_path(partial, reread, &bytes) || bytes != used ||
        memcmp(reread, digest, 32))
      err = ESP_ERR_INVALID_CRC;
  }
  if (!err) {
    state.total = used;
    state.complete = 1;
    memcpy(state.digest, digest, 32);
    err = launcher_store_write(key, &state, sizeof(state));
    if (!err && rename(partial, path))
      err = ESP_FAIL;
  }
  return err;
}
