#include "cJSON.h"
#include "catalog.h"
#include "json_limits.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool token(const char *s) {
  if (!s || !*s || strlen(s) > 95 || strstr(s, ".."))
    return false;
  for (; *s; s++)
    if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_' && *s != '.')
      return false;
  return true;
}
bool catalog_https_url(const char *s) {
  if (!s || strncmp(s, "https://", 8) || strlen(s) >= 512)
    return false;
  const char *host = s + 8, *end = strchr(host, '/');
  if (!end)
    end = s + strlen(s);
  if (end == host)
    return false;
  for (const char *p = host; p < end; p++)
    if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-' && *p != ':')
      return false;
  for (const char *p = end; *p; p++)
    if ((unsigned char)*p <= 32 || *p == 127 || *p == '#' || *p == '\\')
      return false;
  return true;
}
static bool shape(const cJSON *o, const char *keys) {
  if (!cJSON_IsObject(o))
    return false;
  for (const cJSON *a = o->child; a; a = a->next) {
    for (const cJSON *b = o->child; b != a; b = b->next)
      if (!strcmp(a->string, b->string))
        return false;
    char match[64];
    if (strlen(a->string) > 60)
      return false;
    snprintf(match, sizeof(match), "|%s|", a->string);
    if (!strstr(keys, match))
      return false;
  }
  return true;
}
static bool string(const cJSON *o, const char *key, char *out, size_t n) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
  if (!cJSON_IsString(v) || !*v->valuestring || strlen(v->valuestring) >= n)
    return false;
  for (const unsigned char *p = (const unsigned char *)v->valuestring; *p; p++)
    if (*p < 32)
      return false;
  strcpy(out, v->valuestring);
  return true;
}
esp_err_t catalog_parse(const char *json, catalog_list_t *out) {
  if (out)
    out->count = 0;
  if (!json || !out || strlen(json) > 65536 || !json_depth_valid(json, 8) ||
      strstr(json, "\\u0000"))
    return ESP_ERR_INVALID_ARG;
  out->count = 0;
  cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
  bool ok = shape(root, "|schema||items|");
  cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema"),
        *items = cJSON_GetObjectItemCaseSensitive(root, "items");
  ok = ok && cJSON_IsNumber(schema) && schema->valuedouble == 1 &&
       cJSON_IsArray(items) && cJSON_GetArraySize(items) <= CATALOG_MAX_ITEMS;
  if (ok)
    for (cJSON *i = items->child; i && ok; i = i->next) {
      catalog_item_t *dst = &out->items[out->count];
      memset(dst, 0, sizeof(*dst));
      ok = shape(i, "|id||name||version||description||cover||files|") &&
           string(i, "id", dst->id, sizeof(dst->id)) && token(dst->id) &&
           string(i, "name", dst->name, sizeof(dst->name)) &&
           string(i, "version", dst->version, sizeof(dst->version));
      if (ok && cJSON_GetObjectItemCaseSensitive(i, "description"))
        ok = string(i, "description", dst->description,
                    sizeof(dst->description));
      if (ok && cJSON_GetObjectItemCaseSensitive(i, "cover"))
        ok = string(i, "cover", dst->cover_url, sizeof(dst->cover_url)) &&
             catalog_https_url(dst->cover_url);
      for (unsigned n = 0; n < out->count; n++)
        if (!strcmp(out->items[n].id, dst->id))
          ok = false;
      cJSON *files = cJSON_GetObjectItemCaseSensitive(i, "files");
      ok = ok && cJSON_IsArray(files) && cJSON_GetArraySize(files) >= 1 &&
           cJSON_GetArraySize(files) <= CATALOG_MAX_PARTS;
      if (ok)
        for (cJSON *f = files->child; f && ok; f = f->next) {
          catalog_part_t *p = &dst->parts[dst->count];
          cJSON *bytes = cJSON_GetObjectItemCaseSensitive(f, "size");
          ok = shape(f, "|file||url||sha256||size|") &&
               string(f, "file", p->file, sizeof(p->file)) && token(p->file) &&
               string(f, "url", p->url, sizeof(p->url)) &&
               catalog_https_url(p->url) &&
               string(f, "sha256", p->sha256, sizeof(p->sha256)) &&
               strlen(p->sha256) == 64 && cJSON_IsNumber(bytes) &&
               isfinite(bytes->valuedouble) && bytes->valuedouble >= 1 &&
               bytes->valuedouble <= 32 * 1024 * 1024 &&
               bytes->valuedouble == (uint32_t)bytes->valuedouble;
          if (ok) {
            p->size = (uint32_t)bytes->valuedouble;
            if (ok && cJSON_GetObjectItemCaseSensitive(i, "description"))
              ok = string(i, "description", dst->description,
                          sizeof(dst->description));
            if (ok && cJSON_GetObjectItemCaseSensitive(i, "cover"))
              ok = string(i, "cover", dst->cover_url, sizeof(dst->cover_url)) &&
                   catalog_https_url(dst->cover_url);
            for (unsigned n = 0; n < 64; n++)
              if (!isxdigit((unsigned char)p->sha256[n]))
                ok = false;
          }
          if (ok && cJSON_GetObjectItemCaseSensitive(i, "description"))
            ok = string(i, "description", dst->description,
                        sizeof(dst->description));
          if (ok && cJSON_GetObjectItemCaseSensitive(i, "cover"))
            ok = string(i, "cover", dst->cover_url, sizeof(dst->cover_url)) &&
                 catalog_https_url(dst->cover_url);
          for (unsigned n = 0; n < dst->count; n++)
            if (!strcmp(dst->parts[n].file, p->file))
              ok = false;
          dst->count++;
        }
      if (ok)
        out->count++;
    }
  cJSON_Delete(root);
  if (!ok)
    out->count = 0;
  return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
