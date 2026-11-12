#include "web.h"
#include "board.h"
#include "boot_mgr.h"
#include "cJSON.h"
#include "config_json.h"
#include "diagnostics.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "flash_snapshot.h"
#include "installer.h"
#include "json_limits.h"
#include "launcher_store.h"
#include "pi_services.h"
#include "sd_files.h"
#include "storage.h"
#include "storage_guard.h"
#include "usb_disk.h"
#include "usb_storage.h"
#include "web_ops.h"
#include "web_security.h"
#include <stdatomic.h>

#include "mbedtls/sha256.h"
#include "wifi_mgr.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
static httpd_handle_t s_server;
static void (*s_config_changed)(void);
void web_set_config_changed(void (*fn)(void)) { s_config_changed = fn; }
static char s_token[WEB_TOKEN_LENGTH + 1];
static web_session_t s_session;
static web_login_t s_logins[WEB_LOGIN_SESSIONS];
esp_err_t web_init(void) {
  web_ops_remote_init();
  return ESP_OK;
}
bool web_running(void) { return s_server != NULL; }
void web_access_token(char *out, size_t size) {
  snprintf(out, size, "%s", s_server ? s_token : "");
}
static esp_err_t error(httpd_req_t *r, const char *code, const char *text) {
  httpd_resp_set_status(r, code);
  return httpd_resp_sendstr(r, text);
}
static int cookie_session(httpd_req_t *r) {
  char header[512], secret[33];
  if (httpd_req_get_hdr_value_str(r, "Cookie", header, sizeof(header)) !=
          ESP_OK ||
      !web_cookie_extract(header, secret))
    return -1;
  return web_login_accept(s_logins, secret, esp_timer_get_time());
}
static bool code_valid(httpd_req_t *r) {
  char token[WEB_TOKEN_LENGTH + 2];
  size_t n = httpd_req_get_hdr_value_len(r, "X-Launcher-Token");
  if (n != WEB_TOKEN_LENGTH ||
      httpd_req_get_hdr_value_str(r, "X-Launcher-Token", token,
                                  sizeof(token)) != ESP_OK)
    return web_session_accept(&s_session, NULL, 0, esp_timer_get_time());
  return web_session_accept(&s_session, token, n, esp_timer_get_time());
}
static bool authenticated(httpd_req_t *r) {
  if (cookie_session(r) >= 0) {
    char client[8];
    return r->method == HTTP_GET ||
           (httpd_req_get_hdr_value_str(r, "X-Launcher-Client", client,
                                        sizeof(client)) == ESP_OK &&
            !strcmp(client, "web"));
  }
  return httpd_req_get_hdr_value_len(r, "X-Launcher-Token") > 0 &&
         code_valid(r);
}
static esp_err_t session_api(httpd_req_t *r) {
  if (r->method == HTTP_GET) {
    if (cookie_session(r) < 0)
      return error(r, "401 Unauthorized", "Sessao expirada");
    return httpd_resp_sendstr(r, "{\"authenticated\":true}");
  }
  char client[8];
  if (httpd_req_get_hdr_value_str(r, "X-Launcher-Client", client,
                                  sizeof(client)) != ESP_OK ||
      strcmp(client, "web"))
    return error(r, "403 Forbidden", "Origem invalida");
  if (!strcmp(r->uri, "/api/logout")) {
    int i = cookie_session(r);
    if (i >= 0)
      memset(&s_logins[i], 0, sizeof(s_logins[i]));
    httpd_resp_set_hdr(
        r, "Set-Cookie",
        "tab5_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    return httpd_resp_sendstr(r, "{\"authenticated\":false}");
  }
  if (!code_valid(r))
    return error(r, "401 Unauthorized", "Codigo invalido ou expirado");
  uint8_t random[16];
  char secret[33], cookie[160];
  esp_fill_random(random, sizeof(random));
  for (unsigned i = 0; i < 16; i++)
    snprintf(secret + 2 * i, 3, "%02x", random[i]);
  web_login_issue(s_logins, secret, esp_timer_get_time());
  snprintf(cookie, sizeof(cookie),
           "tab5_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800",
           secret);
  httpd_resp_set_hdr(r, "Set-Cookie", cookie);
  return httpd_resp_sendstr(r, "{\"authenticated\":true}");
}
static esp_err_t json(httpd_req_t *r, cJSON *value) {
  char *body = cJSON_PrintUnformatted(value);
  cJSON_Delete(value);
  if (!body)
    return error(r, "503 Service Unavailable", "Sem memoria");
  httpd_resp_set_type(r, "application/json");
  esp_err_t e = httpd_resp_sendstr(r, body);
  free(body);
  return e;
}
/* URI decoding is single-pass, with explicit NUL/control rejection. */
static bool query(httpd_req_t *r, const char *key, char *out, size_t size) {
  char raw[1500], encoded[1536];
  if (httpd_req_get_url_query_str(r, raw, sizeof(raw)) != ESP_OK ||
      httpd_query_key_value(raw, key, encoded, sizeof(encoded)) != ESP_OK)
    return false;
  return web_query_decode(encoded, out, size);
}
static esp_err_t index_get(httpd_req_t *r) {
  STORAGE_IO_GUARD(ESP_ERR_INVALID_STATE);
  const char *name = !strcmp(r->uri, "/transfers.js") ? "transfers.js"
                     : !strcmp(r->uri, "/style.css")  ? "style.css"
                     : !strcmp(r->uri, "/app.js")     ? "app.js"
                                                      : "index.html";
  char path[160];
  snprintf(path, sizeof(path), LAUNCHER_WEB "/%s", name);
  FILE *f = fopen(path, "rb");
  if (!f)
    return error(
        r, "503 Service Unavailable",
        "WebUI ausente. Copie os arquivos para /launcher/web no microSD.");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(
      r, "Content-Security-Policy",
      "default-src 'self'; script-src 'self'; style-src 'self'; connect-src "
      "'self'; frame-ancestors 'none'; object-src 'none'");
  httpd_resp_set_type(
      r, !strcmp(name, "style.css") ? "text/css; charset=utf-8"
         : (!strcmp(name, "app.js") || !strcmp(name, "transfers.js"))
             ? "application/javascript; charset=utf-8"
             : "text/html; charset=utf-8");
  char *buffer = malloc(2048);
  if (!buffer) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  esp_err_t e = ESP_OK;
  size_t n;
  while (e == ESP_OK && (n = fread(buffer, 1, 2048, f)) > 0)
    e = httpd_resp_send_chunk(r, buffer, n);
  if (ferror(f))
    e = ESP_FAIL;
  free(buffer);
  fclose(f);
  return e == ESP_OK ? httpd_resp_send_chunk(r, NULL, 0) : e;
}
static esp_err_t status_get(httpd_req_t *r) {
  diagnostics_snapshot_t d;
  diagnostics_get_snapshot(&d);
  wifi_status_t w;
  wifi_get_status(&w);
  board_power_status_t p;
  board_power_status(&p);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "version", d.app_ver);
  cJSON_AddStringToObject(o, "reset", d.reset_desc);
  cJSON_AddNumberToObject(o, "internalFree", d.free_internal);
  cJSON_AddNumberToObject(o, "internalMinimum", d.min_free_internal);
  cJSON_AddNumberToObject(o, "psramFree", d.free_psram);
  cJSON_AddStringToObject(o, "ip", w.ip);
  cJSON_AddNumberToObject(o, "rssi", w.rssi);
  cJSON_AddBoolToObject(o, "installerBusy", installer_is_busy());
  cJSON_AddNumberToObject(o, "usbTransitionError", usb_disk_transition_error());
  if (p.valid) {
    cJSON_AddNumberToObject(o, "batteryMv", p.millivolts);
    cJSON_AddNumberToObject(o, "batteryEstimate", p.percent);
  }
  return json(r, o);
}
static esp_err_t apps_get(httpd_req_t *r) {
  cJSON *a = cJSON_CreateArray();
  for (int i = 0; i < BOOT_MGR_MAX_SLOTS; i++) {
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + i, NULL);
    if (!p)
      continue;
    esp_app_desc_t d;
    bool valid = esp_ota_get_partition_description(p, &d) == ESP_OK;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "slot", i);
    cJSON_AddStringToObject(o, "label", p->label);
    cJSON_AddStringToObject(o, "name", valid ? d.project_name : "");
    cJSON_AddStringToObject(o, "version", valid ? d.version : "");
    cJSON_AddBoolToObject(o, "descriptorValid", valid);
    cJSON_AddItemToArray(a, o);
  }
  return json(r, a);
}
static esp_err_t flash_layout_get(httpd_req_t *r) {
  if (installer_is_busy() || web_ops_busy())
    return error(r, "409 Conflict", "Operacao em andamento");
  flash_allocation_t *layout = malloc(sizeof(*layout));
  if (!layout)
    return error(r, "503 Service Unavailable", "Sem memoria");
  char message[256] = {0};
  esp_err_t e = flash_allocate_plan(0, layout, message, sizeof(message));
  if (e) {
    free(layout);
    return error(r, "409 Conflict", message[0] ? message : esp_err_to_name(e));
  }
  cJSON *o = cJSON_CreateObject(), *sizes = cJSON_AddArrayToObject(o, "sizes"),
        *minimum = cJSON_AddArrayToObject(o, "minimum");
  cJSON_AddNumberToObject(o, "launcher", layout->launcher_size);
  cJSON_AddNumberToObject(o, "capacity",
                          0xa10000 - 0x20000 - layout->launcher_size);
  cJSON_AddNumberToObject(o, "data", 0x5f0000);
  cJSON_AddNumberToObject(o, "boot", 0x20000);
  for (unsigned i = 0; i < 16; i++) {
    cJSON_AddItemToArray(sizes, cJSON_CreateNumber(layout->reserved[i]));
    cJSON_AddItemToArray(minimum, cJSON_CreateNumber(layout->minimum[i]));
  }
  free(layout);
  return json(r, o);
}
static esp_err_t files_get(httpd_req_t *r) {
  char path[SD_PATH_MAX], page[16], all_param[4];
  unsigned n = 0;
  if (!query(r, "path", path, sizeof(path)) || !sd_path_valid(path))
    return error(r, "400 Bad Request", "Caminho invalido");
  if (web_path_is_private(path))
    return error(r, "403 Forbidden", "Pasta privada protegida");
  if (query(r, "all", all_param, sizeof(all_param)) &&
      !strcmp(all_param, "1")) {
    sd_entry_t *entries = NULL;
    unsigned count = 0;
    int err = sd_files_list_all(path, &entries, &count, false);
    if (err)
      return error(r, "409 Conflict", strerror(err));
    char prefix[80];
    snprintf(prefix, sizeof(prefix), "{\"total\":%u,\"entries\":[", count);
    httpd_resp_set_type(r, "application/json");
    esp_err_t result = httpd_resp_send_chunk(r, prefix, strlen(prefix));
    for (unsigned i = 0; !result && i < count; i++) {
      cJSON *item = cJSON_CreateObject();
      cJSON_AddStringToObject(item, "name", entries[i].name);
      cJSON_AddNumberToObject(item, "size", entries[i].size);
      cJSON_AddBoolToObject(item, "directory", entries[i].directory);
      char *text = cJSON_PrintUnformatted(item);
      cJSON_Delete(item);
      if (!text) {
        result = ESP_ERR_NO_MEM;
        break;
      }
      if (i)
        result = httpd_resp_send_chunk(r, ",", 1);
      if (!result)
        result = httpd_resp_send_chunk(r, text, strlen(text));
      free(text);
    }
    free(entries);
    if (!result)
      result = httpd_resp_send_chunk(r, "]}", 2);
    return result ? result : httpd_resp_send_chunk(r, NULL, 0);
  }
  if (query(r, "page", page, sizeof(page))) {
    char *end;
    unsigned long p = strtoul(page, &end, 10);
    if (*end || p > 100000)
      return error(r, "400 Bad Request", "Pagina invalida");
    n = p;
  }
  char focus[SD_NAME_MAX];
  bool has_focus = query(r, "focus", focus, sizeof(focus));
  sd_listing_t *list = malloc(sizeof(*list));
  if (!list)
    return error(r, "503 Service Unavailable", "Sem memoria");
  if (sd_files_list_filtered(path, n, list, has_focus ? focus : NULL, false)) {
    free(list);
    return error(r, "404 Not Found", "Diretorio indisponivel");
  }
  cJSON *o = cJSON_CreateObject(), *a = cJSON_AddArrayToObject(o, "entries");
  cJSON_AddNumberToObject(o, "total", list->total);
  cJSON_AddNumberToObject(o, "page", list->page);
  for (unsigned i = 0; i < list->count; i++) {
    sd_entry_t *f = &list->entries[i];
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", f->name);
    cJSON_AddNumberToObject(e, "size", f->size);
    cJSON_AddBoolToObject(e, "directory", f->directory);
    cJSON_AddItemToArray(a, e);
  }
  free(list);
  return json(r, o);
}
static esp_err_t file_get_impl(httpd_req_t *r) {
  char path[SD_PATH_MAX];
  if (!query(r, "path", path, sizeof(path)) || !sd_path_valid(path))
    return error(r, "400 Bad Request", "Caminho invalido");
  if (web_path_is_private(path))
    return error(r, "403 Forbidden", "Configuracao privada protegida");
  struct stat st;
  if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size > 32 * 1024 * 1024)
    return error(r, "404 Not Found", "Arquivo ausente ou maior que 32 MiB");
  FILE *f = fopen(path, "rb");
  if (!f)
    return error(r, "404 Not Found", "Arquivo indisponivel");
  char *buf = malloc(4096);
  if (!buf) {
    fclose(f);
    return ESP_ERR_NO_MEM;
  }
  httpd_resp_set_type(r, "application/octet-stream");
  httpd_resp_set_hdr(r, "X-Content-Type-Options", "nosniff");
  size_t n;
  esp_err_t e = ESP_OK;
  while ((n = fread(buf, 1, 4096, f)) && e == ESP_OK)
    e = httpd_resp_send_chunk(r, buf, n);
  if (ferror(f))
    e = ESP_FAIL;
  free(buf);
  fclose(f);
  return e == ESP_OK ? httpd_resp_send_chunk(r, NULL, 0) : e;
}
static bool make_directories(const char *path) {
  char p[SD_PATH_MAX];
  snprintf(p, sizeof(p), "%s", path);
  const char *base = sd_path_root(path);
  if (!base)
    return false;
  size_t root = strlen(base);
  for (char *q = p + root;; q++) {
    if (*q != '/' && *q)
      continue;
    char c = *q;
    *q = 0;
    struct stat st;
    if (stat(p, &st)) {
      if (mkdir(p, 0755))
        return false;
    } else if (!S_ISDIR(st.st_mode))
      return false;
    *q = c;
    if (!c)
      break;
  }
  return true;
}
static bool file_hash(const char *path, uint8_t out[32]) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  uint8_t b[2048];
  size_t n;
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  while ((n = fread(b, 1, sizeof(b), f)))
    mbedtls_sha256_update(&h, b, n);
  bool ok = !ferror(f);
  fclose(f);
  mbedtls_sha256_finish(&h, out);
  mbedtls_sha256_free(&h);
  return ok;
}
static esp_err_t upload_post_impl(httpd_req_t *r) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (installer_is_busy() || web_ops_busy() || installer_recovery_required())
    return error(r, "409 Conflict", "Instalacao em andamento");
  char base[SD_PATH_MAX], relative[SD_PATH_MAX], path[SD_PATH_MAX];
  if (!query(r, "directory", base, sizeof(base)))
    snprintf(base, sizeof(base), "%s", SD_FILES_ROOT);
  if (!query(r, "relative", relative, sizeof(relative)) &&
      !query(r, "name", relative, sizeof(relative)))
    return error(r, "400 Bad Request", "Nome ausente");
  if (!sd_path_valid(base) ||
      !web_upload_path(base, relative, path, sizeof(path)) ||
      !sd_path_valid(path) || strlen(path) > 475 ||
      r->content_len > 16 * 1024 * 1024)
    return error(r, "400 Bad Request",
                 "Destino invalido ou arquivo maior que 16 MiB");
  char parent[SD_PATH_MAX], temp[SD_PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", path);
  *strrchr(parent, '/') = 0;
  if (!make_directories(parent))
    return error(r, "507 Insufficient Storage",
                 "Nao foi possivel criar pastas");
  snprintf(temp, sizeof(temp), "%.*s/.upload-%08lx.part",
           (int)sizeof(temp) - 32, parent, (unsigned long)esp_random());
  FILE *f = fopen(temp, "wx");
  if (!f)
    return error(r, "507 Insufficient Storage", "Falha ao criar temporario");
  char *buf = malloc(4096);
  bool ok = buf != NULL;
  size_t used = 0;
  int64_t deadline = esp_timer_get_time() + 600000000;
  mbedtls_sha256_context h;
  mbedtls_sha256_init(&h);
  mbedtls_sha256_starts(&h, 0);
  while (ok && used < r->content_len) {
    if (esp_timer_get_time() > deadline) {
      ok = false;
      break;
    }
    size_t need = r->content_len - used;
    if (need > 4096)
      need = 4096;
    int n = httpd_req_recv(r, buf, need);
    if (n == HTTPD_SOCK_ERR_TIMEOUT)
      continue;
    if (n <= 0 || fwrite(buf, 1, n, f) != (size_t)n) {
      ok = false;
      break;
    }
    mbedtls_sha256_update(&h, (uint8_t *)buf, n);
    used += n;
  }
  free(buf);
  uint8_t digest[32], actual[32];
  mbedtls_sha256_finish(&h, digest);
  mbedtls_sha256_free(&h);
  if (fflush(f) || fsync(fileno(f)))
    ok = false;
  if (fclose(f))
    ok = false;
  if (!ok) {
    unlink(temp);
    return error(r, "500 Internal Server Error",
                 "Envio incompleto. Arquivo nao publicado; tente reenviar.");
  }
  bool exists = access(path, F_OK) == 0;
  if (!file_hash(temp, actual) || memcmp(actual, digest, 32)) {
    unlink(temp);
    return error(
        r, "500 Internal Server Error",
        "Arquivo recebido falhou na verificacao; original preservado.");
  }
  char previous[SD_PATH_MAX];
  snprintf(previous, sizeof(previous), "%.*s/.upload-%08lx.previous",
           (int)sizeof(previous) - 40, parent, (unsigned long)esp_random());
  if (exists && rename(path, previous)) {
    unlink(temp);
    return error(r, "409 Conflict",
                 "Nao foi possivel preservar a versao anterior.");
  }
  if (rename(temp, path)) {
    if (exists)
      rename(previous, path);
    unlink(temp);
    return error(r, "500 Internal Server Error",
                 "Falha ao substituir arquivo; original preservado.");
  }
  if (!file_hash(path, actual) || memcmp(actual, digest, 32)) {
    unlink(path);
    if (exists)
      rename(previous, path);
    return error(r, "500 Internal Server Error",
                 "Falha de verificacao; versao anterior restaurada.");
  }
  if (exists)
    unlink(previous);
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "path", path);
  cJSON_AddStringToObject(o, "directory", parent);
  cJSON_AddNumberToObject(o, "size", used);
  cJSON_AddBoolToObject(o, "verified", true);
  cJSON_AddBoolToObject(o, "alreadyPresent", exists);
  cJSON_AddStringToObject(o, "message",
                          exists ? "Arquivo substituido e verificado"
                                 : "Arquivo enviado e verificado no cartao");
  return json(r, o);
}
static esp_err_t upload_post(httpd_req_t *r) {
  char p[SD_PATH_MAX];
  if (!query(r, "directory", p, sizeof(p)))
    snprintf(p, sizeof(p), "%s", SD_FILES_ROOT);
  if (!sd_media_acquire(p))
    return error(r, "409 Conflict", "Unidade indisponivel");
  esp_err_t e = upload_post_impl(r);
  sd_media_release(p);
  return e;
}
static esp_err_t file_get(httpd_req_t *r) {
  char p[SD_PATH_MAX];
  if (!query(r, "path", p, sizeof(p)) || !sd_media_acquire(p))
    return error(r, "409 Conflict", "Unidade indisponivel");
  esp_err_t e = file_get_impl(r);
  sd_media_release(p);
  return e;
}
static esp_err_t mkdir_post(httpd_req_t *r) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  char base[SD_PATH_MAX], relative[SD_PATH_MAX], path[SD_PATH_MAX];
  if (installer_is_busy() || web_ops_busy() || installer_recovery_required())
    return error(r, "409 Conflict", "Instalacao em andamento");
  if (!query(r, "directory", base, sizeof(base)) ||
      !query(r, "relative", relative, sizeof(relative)) ||
      !sd_path_valid(base) ||
      !web_upload_path(base, relative, path, sizeof(path)) ||
      !sd_path_valid(path))
    return error(r, "400 Bad Request", "Pasta invalida");
  if (!make_directories(path))
    return error(r, "507 Insufficient Storage", "Falha ao criar pasta");
  return httpd_resp_sendstr(r, "{\"created\":true}");
}
static esp_err_t diagnostic_post(httpd_req_t *r, bool screenshot) {
  char path[128];
  snprintf(path, sizeof(path), LAUNCHER_DIAGNOSTICS "/%s-%08lx.%s",
           screenshot ? "screenshot" : "diagnostics",
           (unsigned long)esp_random(), screenshot ? "bmp" : "log");
  esp_err_t e =
      screenshot ? diagnostics_capture(path) : diagnostics_export_log(path);
  if (e)
    return error(r, "500 Internal Server Error", esp_err_to_name(e));
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "path", path);
  return json(r, o);
}
static bool body_json(httpd_req_t *r, char *text, size_t size) {
  if (!r->content_len || r->content_len >= size)
    return false;
  size_t used = 0;
  while (used < r->content_len) {
    int n = httpd_req_recv(r, text + used, r->content_len - used);
    if (n <= 0)
      return false;
    used += n;
  }
  text[used] = 0;
  return !memchr(text, 0, used);
}
static bool mutable_path(const char *path) {
  if (path && !strcmp(path, LAUNCHER_ROOT "/ota_sources.json"))
    return true;
  if (!sd_path_valid(path) || !strcmp(path, SD_FILES_ROOT) ||
      web_path_is_private(path))
    return false;
  /* Protect live launcher, active operation snapshots and downloaded firmware
   * metadata from ancestor renames/deletes as well as direct access. */
  const char *protected[] = {SD_FILES_ROOT "/launcher",
                             SD_FILES_ROOT "/backups"};
  for (unsigned i = 0; i < 2; i++) {
    size_t n = strlen(protected[i]);
    if (!strncasecmp(path, protected[i], n) && (!path[n] || path[n] == '/'))
      return false;
  }
  return true;
}
static esp_err_t file_preview(httpd_req_t *r) {
  char path[SD_PATH_MAX];
  if (!query(r, "path", path, sizeof(path)) || !mutable_path(path))
    return error(r, "403 Forbidden", "Arquivo protegido ou caminho invalido");
  char *text = malloc(4096);
  if (!text)
    return ESP_ERR_NO_MEM;
  bool truncated = false;
  int err = sd_files_preview(path, text, 4096, &truncated);
  if (err) {
    free(text);
    return error(r, "409 Conflict", strerror(err));
  }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "text", text);
  cJSON_AddBoolToObject(o, "truncated", truncated);
  free(text);
  return json(r, o);
}
static esp_err_t file_action(httpd_req_t *r) {
  if (installer_is_busy() || web_ops_busy() || installer_recovery_required())
    return error(r, "409 Conflict", "Instalacao em andamento");
  char *text = malloc(27000);
  if (!text)
    return ESP_ERR_NO_MEM;
  bool ok = body_json(r, text, 27000) && json_depth_valid(text, 4);
  cJSON *j = ok ? cJSON_ParseWithOpts(text, NULL, true) : NULL;
  free(text);
  cJSON *kind = cJSON_GetObjectItemCaseSensitive(j, "kind"),
        *path = cJSON_GetObjectItemCaseSensitive(j, "path"),
        *value = cJSON_GetObjectItemCaseSensitive(j, "value");
  int err = EINVAL;
  if (cJSON_IsObject(j) && cJSON_IsString(kind) && cJSON_IsString(path) &&
      sd_path_valid(path->valuestring)) {
    const char *p = path->valuestring,
               *v = cJSON_IsString(value) ? value->valuestring : "";
    if (!strcmp(kind->valuestring, "mkdir")) {
      char dest[SD_PATH_MAX];
      if (sd_path_join(dest, sizeof(dest), p, v) && mutable_path(dest))
        err = sd_files_mkdir(p, v);
    } else if (mutable_path(p)) {
      if (!strcmp(kind->valuestring, "delete"))
        err = sd_files_delete_recursive(p);
      else if (!strcmp(kind->valuestring, "rename")) {
        char parent[SD_PATH_MAX], dest[SD_PATH_MAX];
        snprintf(parent, sizeof(parent), "%s", p);
        *strrchr(parent, '/') = 0;
        if (sd_path_join(dest, sizeof(dest), parent, v) && mutable_path(dest))
          err = sd_files_rename(p, v);
      } else if (!strcmp(kind->valuestring, "copy")) {
        char dest[SD_PATH_MAX];
        if (!strncmp(v, "C:", 2))
          snprintf(dest, sizeof(dest), SD_FILES_ROOT "%s", v + 2);
        else if (!strncmp(v, "D:", 2))
          snprintf(dest, sizeof(dest), USB_FILES_ROOT "%s", v + 2);
        else
          snprintf(dest, sizeof(dest), "%s", v);
        if (sd_path_valid(dest) && !web_path_is_private(dest))
          err = sd_files_copy(p, dest);
      } else if (!strcmp(kind->valuestring, "edit")) {
        cJSON *expected = cJSON_GetObjectItemCaseSensitive(j, "expected"),
              *replacement = cJSON_GetObjectItemCaseSensitive(j, "text");
        if (cJSON_IsString(expected) && cJSON_IsString(replacement))
          err =
              sd_files_edit(p, expected->valuestring, replacement->valuestring);
      }
    }
  }
  cJSON_Delete(j);
  if (err)
    return error(r, "409 Conflict", strerror(err));
  return httpd_resp_sendstr(r, "{\"ok\":true}");
}
static esp_err_t config_api(httpd_req_t *r) {
  launcher_config_t c;
  storage_snapshot(&c);
  if (r->method == HTTP_POST) {
    char *text = malloc(4096);
    if (!text)
      return ESP_ERR_NO_MEM;
    bool ok = body_json(r, text, 4096);
    esp_err_t e = ok ? config_json_decode(text, &c) : ESP_ERR_INVALID_ARG;
    free(text);
    if (!e)
      e = storage_update(&c);
    if (e)
      return error(r, "400 Bad Request", esp_err_to_name(e));
    board_backlight(c.bright);
    if (s_config_changed)
      s_config_changed();
  }
  char *text = config_json_encode(&c);
  if (!text)
    return ESP_ERR_NO_MEM;
  httpd_resp_set_type(r, "application/json");
  esp_err_t e = httpd_resp_sendstr(r, text);
  free(text);
  return e;
}
static esp_err_t power_calibration_api(httpd_req_t *r) {
  power_calibration_t c;
  board_power_calibration_get(&c);
  if (r->method == HTTP_POST) {
    char text[2048];
    if (!body_json(r, text, sizeof(text)) || !json_depth_valid(text, 4))
      return error(r, "400 Bad Request", "Calibracao invalida");
    cJSON *j = cJSON_ParseWithOpts(text, NULL, true),
          *gain = cJSON_GetObjectItemCaseSensitive(j, "gainPpm"),
          *voltage = cJSON_GetObjectItemCaseSensitive(j, "voltageOffsetMv"),
          *current = cJSON_GetObjectItemCaseSensitive(j, "currentOffsetMa"),
          *points = cJSON_GetObjectItemCaseSensitive(j, "points");
    bool ok =
        cJSON_IsNumber(gain) && gain->valuedouble == gain->valueint &&
        cJSON_IsNumber(voltage) && voltage->valuedouble == voltage->valueint &&
        cJSON_IsNumber(current) && current->valuedouble == current->valueint &&
        cJSON_IsArray(points) &&
        cJSON_GetArraySize(points) <= POWER_CURVE_POINTS;
    if (ok) {
      memset(&c, 0, sizeof(c));
      c.gain_ppm = gain->valueint;
      c.voltage_offset_mv = voltage->valueint;
      c.current_offset_ma = current->valueint;
      c.count = cJSON_GetArraySize(points);
      for (unsigned i = 0; i < c.count; i++) {
        cJSON *point = cJSON_GetArrayItem(points, i),
              *mv = cJSON_GetObjectItemCaseSensitive(point, "mv"),
              *pc = cJSON_GetObjectItemCaseSensitive(point, "percent");
        if (!cJSON_IsNumber(mv) || mv->valuedouble != mv->valueint ||
            !cJSON_IsNumber(pc) || pc->valuedouble != pc->valueint) {
          ok = false;
          break;
        }
        c.points[i] = (power_curve_point_t){mv->valueint, pc->valueint};
      }
    }
    cJSON_Delete(j);
    esp_err_t e = ok ? board_power_calibration_set(&c) : ESP_ERR_INVALID_ARG;
    if (e)
      return error(r, "400 Bad Request", esp_err_to_name(e));
  }
  cJSON *out = cJSON_CreateObject();
  cJSON_AddNumberToObject(out, "gainPpm", c.gain_ppm);
  cJSON_AddNumberToObject(out, "voltageOffsetMv", c.voltage_offset_mv);
  cJSON_AddNumberToObject(out, "currentOffsetMa", c.current_offset_ma);
  cJSON *points = cJSON_AddArrayToObject(out, "points");
  for (unsigned i = 0; i < c.count; i++) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "mv", c.points[i].millivolts);
    cJSON_AddNumberToObject(p, "percent", c.points[i].percent);
    cJSON_AddItemToArray(points, p);
  }
  return json(r, out);
}
static esp_err_t app_action(httpd_req_t *r) {
  char text[256];
  if (!body_json(r, text, sizeof(text)))
    return error(r, "400 Bad Request", "Pedido invalido");
  if (!json_depth_valid(text, 4))
    return error(r, "400 Bad Request", "JSON muito aninhado");
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
  cJSON *slot = cJSON_GetObjectItemCaseSensitive(j, "slot"),
        *action = cJSON_GetObjectItemCaseSensitive(j, "action");
  if (!cJSON_IsObject(j) || !cJSON_IsNumber(slot) || slot->valuedouble < 0 ||
      slot->valuedouble >= BOOT_MGR_MAX_SLOTS ||
      slot->valuedouble != (int)slot->valuedouble || !cJSON_IsString(action)) {
    cJSON_Delete(j);
    return error(r, "400 Bad Request", "Slot/acao invalidos");
  }
  esp_err_t e = ESP_ERR_INVALID_ARG;
  char message[512] = "";
  if (!strcmp(action->valuestring, "boot")) {
    e = boot_mgr_launch_async(slot->valueint, NULL, NULL);
    snprintf(
        message, sizeof(message),
        "Inicio solicitado; o aparelho verifica a imagem antes de reiniciar.");
  } else if (!strcmp(action->valuestring, "default")) {
    e = boot_mgr_set_default(slot->valueint);
    snprintf(message, sizeof(message), "App padrao salvo no microSD");
  } else if (!strcmp(action->valuestring, "backup"))
    e = installer_backup_slot(slot->valueint, NULL, NULL, message,
                              sizeof(message));
  cJSON_Delete(j);
  if (e)
    return error(r, "409 Conflict", esp_err_to_name(e));
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "message", message);
  return json(r, o);
}
static esp_err_t api_handler_impl(httpd_req_t *r) {
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  if (!strcmp(r->uri, "/api/session") || !strcmp(r->uri, "/api/logout"))
    return session_api(r);
  if (!authenticated(r))
    return error(r, "401 Unauthorized", "Token ausente ou invalido");
  if (r->method == HTTP_GET && !strcmp(r->uri, "/api/flash-layout"))
    return flash_layout_get(r);

  if (r->method == HTTP_POST && !strncmp(r->uri, "/api/mkdir?", 11))
    return mkdir_post(r);
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/usb-eject")) {
    esp_err_t e = usb_storage_eject();
    if (e)
      return error(r, "409 Conflict", esp_err_to_name(e));
    return httpd_resp_sendstr(r, "{\"ok\":true,\"message\":\"Ejetando D: "
                                 "aguarde o fim das operacoes\"}");
  }
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/usb-disk")) {
    if (installer_is_busy() || web_ops_busy() || installer_recovery_required())
      return error(r, "409 Conflict", "Operacao em andamento");
    esp_err_t e = usb_disk_request();
    if (e)
      return error(r, "409 Conflict", esp_err_to_name(e));
    return httpd_resp_sendstr(r, "{\"ok\":true,\"message\":\"Solicitacao USB "
                                 "aceita; aguardando armazenamento ocioso.\"}");
  }
  if (r->method == HTTP_GET && !strncmp(r->uri, "/api/pi?", 8)) {
    char kind[20];
    if (!query(r, "kind", kind, sizeof(kind)) ||
        (strcmp(kind, "apps") && strcmp(kind, "backups")))
      return error(r, "400 Bad Request", "Lista invalida");
    char *text = malloc(16385);
    if (!text)
      return ESP_ERR_NO_MEM;
    esp_err_t e = pi_services_list(!strcmp(kind, "backups"), text, 16385);
    if (e) {
      free(text);
      return error(r, "409 Conflict", esp_err_to_name(e));
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "text", text);
    free(text);
    return json(r, o);
  }
  if (r->method == HTTP_GET && !strcmp(r->uri, "/api/operation"))
    return json(r, web_ops_status());
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/operation")) {
    char body[2048];
    if (!body_json(r, body, sizeof(body)) || !json_depth_valid(body, 4))
      return error(r, "400 Bad Request", "Pedido invalido");
    cJSON *j = cJSON_ParseWithOpts(body, NULL, true), *out = web_ops_request(j);
    cJSON_Delete(j);
    return json(r, out);
  }
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/file-action"))
    return file_action(r);
  if (r->method == HTTP_GET && !strncmp(r->uri, "/api/file-preview?", 18))
    return file_preview(r);
  if (!strcmp(r->uri, "/api/power-calibration"))
    return power_calibration_api(r);
  if (!strcmp(r->uri, "/api/config"))
    return config_api(r);
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/app-action"))
    return app_action(r);
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/screenshot"))
    return diagnostic_post(r, true);
  if (r->method == HTTP_POST && !strcmp(r->uri, "/api/log"))
    return diagnostic_post(r, false);
  if (!strncmp(r->uri, "/api/status", 11))
    return status_get(r);
  if (!strncmp(r->uri, "/api/apps", 9))
    return apps_get(r);
  if (!strncmp(r->uri, "/api/files?", 11))
    return files_get(r);
  if (!strncmp(r->uri, "/api/file?", 10))
    return file_get(r);
  if (r->method == HTTP_POST && !strncmp(r->uri, "/api/upload?", 12))
    return upload_post(r);
  return error(r, "404 Not Found", "Endpoint inexistente");
}
static atomic_uint requests;
bool web_io_busy(void) { return atomic_load(&requests) > 0 || web_ops_busy(); }
static esp_err_t api_handler(httpd_req_t *r) {
  bool transition =
      !strcmp(r->uri, "/api/usb-disk") || !strcmp(r->uri, "/api/app-action");
  storage_io_scope_t scope
      __attribute__((cleanup(storage_io_scope_leave))) = {false};
  if (!transition) {
    scope.held = storage_io_acquire();
    if (!scope.held)
      return error(r, "409 Conflict",
                   "Armazenamento reservado para troca de modo");
  }
  atomic_fetch_add(&requests, 1);
  esp_err_t e = api_handler_impl(r);
  atomic_fetch_sub(&requests, 1);
  return e;
}
esp_err_t web_start(void) {
  if (s_server)
    return ESP_OK;
  wifi_status_t w;
  wifi_get_status(&w);
  if (w.state != WIFI_STATE_CONNECTED)
    return ESP_ERR_INVALID_STATE;
  snprintf(s_token, sizeof(s_token), "%08lu",
           (unsigned long)(esp_random() % 100000000));
  web_session_begin(&s_session, s_token, esp_timer_get_time());
  web_logins_reset(s_logins);
  httpd_config_t c = HTTPD_DEFAULT_CONFIG();
  c.stack_size = 16384;
  c.max_open_sockets = 3;
  c.lru_purge_enable = true;
  c.uri_match_fn = httpd_uri_match_wildcard;
  c.recv_wait_timeout = 5;
  c.send_wait_timeout = 5;
  esp_err_t e = httpd_start(&s_server, &c);
  if (e) {
    s_server = NULL;
    memset(s_token, 0, sizeof(s_token));
    return e;
  }
  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = index_get};
  httpd_uri_t css = {
      .uri = "/style.css", .method = HTTP_GET, .handler = index_get};
  httpd_uri_t js = {.uri = "/app.js", .method = HTTP_GET, .handler = index_get};
  httpd_uri_t transfers = {
      .uri = "/transfers.js", .method = HTTP_GET, .handler = index_get};
  httpd_uri_t get = {
      .uri = "/api/*", .method = HTTP_GET, .handler = api_handler};
  httpd_uri_t post = {
      .uri = "/api/*", .method = HTTP_POST, .handler = api_handler};
  e = httpd_register_uri_handler(s_server, &root);
  if (!e)
    e = httpd_register_uri_handler(s_server, &css);
  if (!e)
    e = httpd_register_uri_handler(s_server, &js);
  if (!e)
    e = httpd_register_uri_handler(s_server, &transfers);
  if (!e)
    e = httpd_register_uri_handler(s_server, &get);
  if (!e)
    e = httpd_register_uri_handler(s_server, &post);
  if (e)
    web_stop();
  return e;
}
void web_stop(void) {
  if (s_server) {
    httpd_stop(s_server);
    s_server = NULL;
  }
  memset(s_token, 0, sizeof(s_token));
  memset(&s_session, 0, sizeof(s_session));
  web_logins_reset(s_logins);
}
