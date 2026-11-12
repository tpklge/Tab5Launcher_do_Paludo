#include "storage_guard.h"
#include "catalog.h"
#include "board.h"
#include "catalog_tracking.h"
#include "catalog_transfer.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

esp_err_t catalog_init(void) { return ESP_OK; }
static esp_http_client_handle_t open_url(const char *url, char *message,
                                         size_t size) {
  if (message && size)
    *message = 0;
  if (!catalog_https_url(url)) {
    if (message)
      snprintf(message, size,
               "URL invalida: use HTTPS para o arquivo JSON, nao a pagina do "
               "projeto.");
    return NULL;
  }
  if (time(NULL) < 1704067200) {
    if (message)
      snprintf(message, size,
               "Ajuste data/hora antes de validar o certificado HTTPS.");
    return NULL;
  }
  /* GitHub/CDN security and redirect headers can exceed the SDK's 2 KiB
   * default. */
  esp_http_client_config_t c = {.url = url,
                                .timeout_ms = 15000,
                                .crt_bundle_attach = esp_crt_bundle_attach,
                                .disable_auto_redirect = true,
                                .buffer_size = 16384,
                                .buffer_size_tx = 1024};
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) {
    if (message)
      snprintf(message, size, "Memoria insuficiente para HTTPS");
    return NULL;
  }
  for (unsigned redirects = 0; redirects <= 5; redirects++) {
    esp_err_t e = esp_http_client_open(h, 0);
    if (e) {
      if (message)
        snprintf(message, size, "Conexao HTTPS falhou: %s (rede/DNS/TLS)",
                 esp_err_to_name(e));
      break;
    }
    if (esp_http_client_fetch_headers(h) < 0) {
      if (message)
        snprintf(message, size,
                 "Resposta HTTPS incompleta: falha ao ler cabecalhos; tente "
                 "novamente.");
      break;
    }
    int code = esp_http_client_get_status_code(h);
    if (code == 200)
      return h;
    if (code != 301 && code != 302 && code != 303 && code != 307 &&
        code != 308) {
      if (message)
        snprintf(message, size, "Servidor respondeu HTTP %d", code);
      break;
    }
    if (redirects == 5 || esp_http_client_set_redirection(h) != ESP_OK) {
      if (message)
        snprintf(message, size, "Redirecionamento ausente ou mais de 5 saltos");
      break;
    }
    char next[512];
    if (esp_http_client_get_url(h, next, sizeof(next)) != ESP_OK ||
        !catalog_https_url(next)) {
      if (message)
        snprintf(message, size,
                 "Redirecionamento recusado: destino precisa usar HTTPS");
      break;
    }
    esp_http_client_close(h);
  }
  esp_http_client_cleanup(h);
  return NULL;
}
static esp_err_t fetch_network(const char *url, catalog_list_t *out,
                               char *error, size_t size,
                               catalog_progress_fn progress, void *user) {
  if (!url || !out)
    return ESP_ERR_INVALID_ARG;
  out->count = 0;
  const char *request_url = url;
  if (!strncmp(url, "https://bmorcelli.github.io/Launcher/catalog.html",
               strlen("https://bmorcelli.github.io/Launcher/catalog.html")))
    request_url = "https://api.launcherhub.net/giveMeTheList";
  bool feed = !strncmp(request_url, "https://api.launcherhub.net/giveMeTheList",
                       strlen("https://api.launcherhub.net/giveMeTheList"));
  feed = feed ||
         !strncmp(request_url, "https://m5burner-api.m5stack.com/api/firmware",
                  strlen("https://m5burner-api.m5stack.com/api/firmware"));
  size_t limit = feed ? 6 * 1024 * 1024 : 65536;
  esp_http_client_handle_t h = open_url(request_url, error, size);
  if (!h)
    return ESP_FAIL;
  char *json = calloc(1, limit + 1);
  if (!json) {
    esp_http_client_cleanup(h);
    return ESP_ERR_NO_MEM;
  }
  size_t used = 0;
  int n = 0;
  unsigned retries = 0;
  bool cancelled = false;
  int64_t expected = esp_http_client_get_content_length(h),
          deadline = esp_timer_get_time() + 600000000;
  ESP_LOGI("catalog", "Consulta iniciada; tamanho anunciado=%lld bytes",
           (long long)expected);
  while (used < limit && esp_timer_get_time() < deadline) {
    if (progress &&
        !progress(expected > 0 ? (unsigned)((uint64_t)used * 95 / expected) : 0,
                  "Consultando catalogo", user)) {
      cancelled = true;
      break;
    }
    size_t need = limit - used;
    if (need > 4096)
      need = 4096;
    n = esp_http_client_read(h, json + used, need);
    if (n == -ESP_ERR_HTTP_EAGAIN && retries++ < 2)
      continue;
    if (n <= 0)
      break;
    used += n;
    retries = 0;
    if ((used / 262144) != ((used - n) / 262144))
      ESP_LOGI("catalog", "Indice recebido: %u KiB", (unsigned)(used / 1024));
  }
  char extra = 0;
  if (used == limit)
    n = esp_http_client_read(h, &extra, 1);
  bool done = !cancelled && n == 0 &&
              esp_http_client_is_complete_data_received(h) &&
              !memchr(json, 0, used);
  esp_err_t err = done        ? catalog_parse_any(json, url, out)
                  : cancelled ? ESP_ERR_INVALID_STATE
                              : ESP_ERR_INVALID_SIZE;
  if (progress && !err)
    progress(100, "Catalogo carregado", user);
  free(json);
  esp_http_client_cleanup(h);
  if (cancelled)
    snprintf(error, size, "Consulta cancelada");
  else if (!done)
    snprintf(error, size,
             "Transferencia interrompida apos %u KiB. Confira a rede e tente "
             "novamente.",
             (unsigned)(used / 1024));
  else
    snprintf(error, size, "%s",
             err == ESP_OK ? "Catalogo carregado"
                           : "Resposta nao e catalogo/manifesto suportado "
                             "(HTML ou estrutura invalida).");
  return err;
}
static esp_err_t fetch_cached(const char *url, catalog_list_t *out, char *error,
                              size_t size, catalog_progress_fn progress,
                              void *user, bool force) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  int64_t age = 0;
  bool have = !force && catalog_cache_read(url, out, &age) == ESP_OK;
  if (have) {
    snprintf(
        error, size,
        "Cache local: %u aplicativos%s. Use Atualizar para consultar a origem.",
        out->count,
        age < 0       ? " (hora desconhecida)"
        : age > 21600 ? " (antigo)"
                      : "");
    if (progress)
      progress(100, "Cache carregado", user);
    return ESP_OK;
  }
  esp_err_t e = fetch_network(url, out, error, size, progress, user);
  if (!e) {
    esp_err_t saved = catalog_cache_write(url, out);
    if (saved)
      snprintf(error, size,
               "Catalogo carregado; nao foi possivel salvar cache no SD");
  } else if (catalog_cache_read(url, out, &age) == ESP_OK) {
    snprintf(error, size,
             "Origem indisponivel; usando cache local de %u aplicativos",
             out->count);
    e = ESP_OK;
  }
  return e;
}
esp_err_t catalog_fetch_progress(const char *u, catalog_list_t *l, char *e,
                                 size_t n, catalog_progress_fn p, void *v) {
  return fetch_cached(u, l, e, n, p, v, false);
}
esp_err_t catalog_fetch_refresh(const char *u, catalog_list_t *l, char *e,
                                size_t n, catalog_progress_fn p, void *v) {
  return fetch_cached(u, l, e, n, p, v, true);
}
esp_err_t catalog_fetch(const char *url, catalog_list_t *out, char *error,
                        size_t size) {
  return catalog_fetch_progress(url, out, error, size, NULL, NULL);
}

esp_err_t catalog_download_from(const catalog_item_t *item, const char *source,
                                char *entry, size_t capacity,
                                catalog_progress_fn cb, void *u) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!item || item->count < 1 || item->count > CATALOG_MAX_PARTS ||
      !board_sd_ready() || !catalog_https_url(source))
    return ESP_ERR_INVALID_ARG;
  char name[80];
  size_t at = 0;
  for (const unsigned char *p = (const unsigned char *)item->name;
       *p && at < 60;) {
    unsigned len = *p < 128              ? 1
                   : (*p & 0xe0) == 0xc0 ? 2
                   : (*p & 0xf0) == 0xe0 ? 3
                   : (*p & 0xf8) == 0xf0 ? 4
                                         : 1;
    if (at + len > 60 || strlen((const char *)p) < len)
      break;
    for (unsigned j = 0; j < len; j++) {
      unsigned char c = *p++;
      name[at++] = c < 32 || strchr("<>:\"/\\|?*", c) ? '-' : c;
    }
  }
  while (at && (name[at - 1] == ' ' || name[at - 1] == '.'))
    at--;
  name[at] = 0;
  if (!at)
    strcpy(name, "firmware");
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, (const uint8_t *)source, strlen(source));
  mbedtls_sha256_update(&sha, (const uint8_t *)item->id, strlen(item->id));
  mbedtls_sha256_update(&sha, (const uint8_t *)item->version,
                        strlen(item->version));
  for (unsigned i = 0; i < item->count; i++) {
    const catalog_part_t *p = &item->parts[i];
    if (!*p->file || strchr(p->file, '/') || strchr(p->file, '\\') ||
        strstr(p->file, "..") || !catalog_https_url(p->url)) {
      mbedtls_sha256_free(&sha);
      return ESP_ERR_INVALID_ARG;
    }
    mbedtls_sha256_update(&sha, (const uint8_t *)p, sizeof(*p));
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  char dir[192], path[320];
  mkdir(LAUNCHER_ROOT "/downloads", 0755);
  snprintf(dir, sizeof(dir),
           LAUNCHER_ROOT "/downloads/%s-%02x%02x%02x%02x%02x%02x%02x%02x", name,
           digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
           digest[6], digest[7]);
  struct stat st;
  if (stat(dir, &st) && mkdir(dir, 0755))
    return ESP_FAIL;
  if (!stat(dir, &st) && !S_ISDIR(st.st_mode))
    return ESP_ERR_INVALID_STATE;
  for (unsigned i = 0; i < item->count; i++) {
    if (cb && !cb(i * 100 / item->count,
                  "Preparando arquivos para download/retomada", u))
      return ESP_ERR_INVALID_STATE;
    char filename[96];
    if (item->count == 1) {
      const char *ext = strrchr(item->parts[i].file, '.');
      snprintf(filename, sizeof(filename), "%s%s", name,
               ext && strlen(ext) < 8 ? ext : ".bin");
    } else
      snprintf(filename, sizeof(filename), "%s", item->parts[i].file);
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    esp_err_t e = catalog_transfer(&item->parts[i], path, cb, u);
    if (e)
      return e;
    if (!i) {
      if (strlen(path) >= capacity)
        return ESP_ERR_INVALID_SIZE;
      snprintf(entry, capacity, "%s", path);
    }
  }
  return catalog_record_download(entry, item, source);
}
esp_err_t catalog_download(const catalog_item_t *i, char *p, size_t n,
                           catalog_progress_fn cb, void *u) {
  char source[512];
  catalog_source_get(source, sizeof(source));
  if (!*source)
    snprintf(source, sizeof(source), "https://local.invalid/catalog");
  return catalog_download_from(i, source, p, n, cb, u);
}
esp_err_t catalog_source_set(const char *url) {
  if (!catalog_https_url(url))
    return ESP_ERR_INVALID_ARG;
  return launcher_store_write("catalog", url, strlen(url) + 1);
}
void catalog_source_get(char *url, size_t size) {
  if (!url || !size)
    return;
  *url = 0;
  size_t n = size;
  esp_err_t e = launcher_store_read("catalog", url, &n);
  if (e != ESP_OK || n > size || !memchr(url, 0, n) || !catalog_https_url(url))
    *url = 0;
}

/* Inspect headers without reading firmware bodies; covers are optional and
 * bounded. */
esp_err_t catalog_probe(catalog_item_t *item, catalog_progress_fn progress,
                        void *user) {
  if (!item)
    return ESP_ERR_INVALID_ARG;
  for (unsigned i = 0; i < item->count; i++) {
    if (progress &&
        !progress(i * 80 / item->count, "Consultando detalhes", user))
      return ESP_ERR_INVALID_STATE;
    if (!item->parts[i].size) {
      esp_http_client_handle_t h = open_url(item->parts[i].url, NULL, 0);
      if (h) {
        int64_t n = esp_http_client_get_content_length(h);
        if (n > 0 && n <= 32 * 1024 * 1024)
          item->parts[i].size = n;
        esp_http_client_cleanup(h);
      }
    }
  }
  if (item->cover_url[0] && board_sd_ready()) {
    mkdir(LAUNCHER_ROOT "/cache", 0755);
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t *)item->cover_url, strlen(item->cover_url),
                   hash, 0);
    char path[160];
    snprintf(path, sizeof(path),
             LAUNCHER_ROOT "/cache/cover-%02x%02x%02x%02x%02x%02x%02x%02x.jpg",
             hash[0], hash[1], hash[2], hash[3], hash[4], hash[5], hash[6],
             hash[7]);
    struct stat st;
    if (!stat(path, &st) && st.st_size > 0) {
      snprintf(item->cover_path, sizeof(item->cover_path), "%s", path);
      return ESP_OK;
    }
    esp_http_client_handle_t h = open_url(item->cover_url, NULL, 0);
    if (h) {
      int64_t size = esp_http_client_get_content_length(h);
      if (size > 0 && size <= 512 * 1024) {
        uint8_t *data = malloc(size);
        if (data) {
          int used = 0, n = 0;
          while (used < size &&
                 (n = esp_http_client_read(
                      h, (char *)data + used,
                      size - used > 4096 ? 4096 : size - used)) > 0) {
            used += n;
            if (progress && !progress(90, "Recebendo imagem", user))
              break;
          }
          bool png = used >= 8 && !memcmp(data, "\x89PNG\r\n\x1a\n", 8),
               jpg = used >= 3 && data[0] == 255 && data[1] == 216 &&
                     data[2] == 255;
          if (used == size && (png || jpg)) {
            if (png)
              strcpy(strrchr(path, '.'), ".png");
            FILE *f = fopen(path, "wb");
            if (f) {
              bool ok = fwrite(data, 1, used, f) == (size_t)used;
              if (fflush(f) || fsync(fileno(f)))
                ok = false;
              if (fclose(f))
                ok = false;
              if (ok)
                snprintf(item->cover_path, sizeof(item->cover_path), "%s",
                         path);
              else
                unlink(path);
            }
          }
          free(data);
        }
      }
      esp_http_client_cleanup(h);
    }
  }
  return ESP_OK;
}

esp_err_t catalog_fetch_latest(const char*u,catalog_list_t*l,char*m,size_t n,catalog_progress_fn cb,void*data){esp_err_t e=fetch_network(u,l,m,n,cb,data);if(!e)catalog_cache_write(u,l);return e;}
