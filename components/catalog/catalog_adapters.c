/* Read-only adapters for the public LauncherHub feed and simple release
 * updates. No proxy identity, device identifier or third-party telemetry is
 * sent. */
#include "cJSON.h"
#include "catalog.h"
#include "json_limits.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
static const char *str(cJSON *j, const char *k) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, k);
  return cJSON_IsString(v) ? v->valuestring : NULL;
}
static bool unique(cJSON *j) {
  for (cJSON *a = j ? j->child : NULL; a; a = a->next)
    for (cJSON *b = j->child; b != a; b = b->next)
      if (a->string && b->string && !strcmp(a->string, b->string))
        return false;
  return true;
}
static void display(char *out, size_t cap, const char *in) {
  size_t n = 0;
  if (!in)
    in = "";
  for (const unsigned char *p = (const unsigned char *)in; *p;) {
    unsigned len = *p < 128              ? 1
                   : (*p & 0xe0) == 0xc0 ? 2
                   : (*p & 0xf0) == 0xe0 ? 3
                   : (*p & 0xf8) == 0xf0 ? 4
                                         : 1;
    if (strlen((const char *)p) < len || n + len >= cap)
      break;
    for (unsigned i = 0; i < len; i++)
      out[n++] = p[i] < 32 ? ' ' : p[i];
    p += len;
  }
  out[n] = 0;
}
static bool part_url(catalog_part_t *p, const char *url) {
  if (!catalog_https_url(url))
    return false;
  snprintf(p->url, sizeof(p->url), "%s", url);
  snprintf(p->file, sizeof(p->file), "firmware.bin");
  return true;
}
static void pin_release(catalog_part_t *p, const char *source) {
  /* A fixed release manifest must not silently download a later mutable build.
   */
  const char *tag = strstr(source, "/releases/download/");
  const char *latest = strstr(p->url, "/releases/latest/download/");
  if (!tag || !latest)
    return;
  size_t prefix = tag - source;
  if ((size_t)(latest - p->url) != prefix || strncmp(source, p->url, prefix))
    return;
  tag += strlen("/releases/download/");
  const char *end = strchr(tag, '/');
  if (!end || end - tag > 95)
    return;
  char fixed[512];
  int n = snprintf(fixed, sizeof(fixed), "%.*s/releases/download/%.*s/%s",
                   (int)prefix, source, (int)(end - tag), tag,
                   latest + strlen("/releases/latest/download/"));
  if (n > 0 && n < (int)sizeof(fixed) && catalog_https_url(fixed))
    strcpy(p->url, fixed);
}
esp_err_t catalog_parse_any(const char *text, const char *source,
                            catalog_list_t *out) {
  if (!text || !source || !out || strlen(text) > 6 * 1024 * 1024 ||
      !json_depth_valid(text, 16) || strstr(text, "\\u0000"))
    return ESP_ERR_INVALID_ARG;
  out->count = 0;
  cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
  if (!root)
    return ESP_ERR_INVALID_ARG;
  if (cJSON_IsObject(root) &&
      cJSON_GetObjectItemCaseSensitive(root, "schema")) {
    cJSON_Delete(root);
    return catalog_parse(text, out);
  }
  bool official =
      !strncmp(source, "https://m5burner-api.m5stack.com/api/firmware",
               strlen("https://m5burner-api.m5stack.com/api/firmware"));
  bool ok = false;
  if (cJSON_IsObject(root) && unique(root) && str(root, "version") &&
      str(root, "url")) {
    catalog_item_t *i = &out->items[0];
    memset(i, 0, sizeof(*i));
    strcpy(i->id, "release-update");
    display(i->version, sizeof(i->version), str(root, "version"));
    display(i->name, sizeof(i->name),
            str(root, "notes") ? str(root, "notes")
                               : "Atualizacao de firmware");
    display(i->description,sizeof(i->description),str(root,"notes"));
    ok = part_url(&i->parts[0], str(root, "url"));
    pin_release(&i->parts[0], source);
    const char *hash = str(root, "sha256");
    if (hash) {
      if (strlen(hash) != 64)
        ok = false;
      else {
        strcpy(i->parts[0].sha256, hash);
        for (unsigned n = 0; n < 64; n++)
          if (!isxdigit((unsigned char)hash[n]))
            ok = false;
      }
    }
    cJSON *size = cJSON_GetObjectItemCaseSensitive(root, "size");
    if (size) {
      double n = size->valuedouble;
      if (!cJSON_IsNumber(size) || !isfinite(n) || n < 1 ||
          n > 32 * 1024 * 1024 || n != (uint32_t)n)
        ok = false;
      else
        i->parts[0].size = (uint32_t)n;
    }
    if (ok) {
      i->count = 1;
      out->count = 1;
    }
  } else if (cJSON_IsArray(root)) {
    ok = true;
    for (cJSON *entry = root->child; entry && out->count < CATALOG_MAX_ITEMS;
         entry = entry->next) {
      const char *category = str(entry, "category"), *chip = str(entry, "esp"),
                 *id = str(entry, "fid"), *name = str(entry, "name");
      if (!category || strcasecmp(category, "tab5") || (!chip && !official) ||
          (chip && strcasecmp(chip, "p4") && strcasecmp(chip, "esp32p4")) ||
          !id || !name)
        continue;
      cJSON *versions = cJSON_GetObjectItemCaseSensitive(entry, "versions"),
            *best = NULL;
      const char *date = "";
      if (!cJSON_IsArray(versions))
        continue;
      for (cJSON *v = versions->child; v; v = v->next) {
        const char *file = str(v, "file"), *version = str(v, "version"),
                   *format = str(cJSON_GetObjectItemCaseSensitive(v, "install"),
                                 "format");
        cJSON *published = cJSON_GetObjectItemCaseSensitive(v, "published");
        if (cJSON_IsFalse(published) || !file || !version ||
            (format && strcmp(format, "app") && strcmp(format, "merged")))
          continue;
        const char *d = str(v, "published_at");
        if (!d)
          d = "";
        if (!best || strcmp(d, date) >= 0) {
          best = v;
          date = d;
        }
      }
      if (!best)
        continue;
      catalog_item_t *i = &out->items[out->count];
      memset(i, 0, sizeof(*i));
      display(i->id, sizeof(i->id), id);
      display(i->name, sizeof(i->name), name);
      display(i->version, sizeof(i->version), str(best, "version"));
      display(i->description,sizeof(i->description),str(entry,"description"));
      const char*cover=str(entry,"cover");if(cover){if(!strncmp(cover,"https://",8))snprintf(i->cover_url,sizeof(i->cover_url),"%s",cover);else snprintf(i->cover_url,sizeof(i->cover_url),"https://m5burner-cdn.m5stack.com/cover/%s",cover);if(!catalog_https_url(i->cover_url))*i->cover_url=0;}
      const char *file = str(best, "file");
      char url[512];
      if (!strncmp(file, "https://", 8))
        snprintf(url, sizeof(url), "%s", file);
      else
        snprintf(url, sizeof(url),
                 "https://m5burner-cdn.m5stack.com/firmware/%s", file);
      if (!part_url(&i->parts[0], url))
        continue;
      i->count = 1;
      out->count++;
    }
  }
  cJSON_Delete(root);
  if (!ok)
    out->count = 0;
  return ok ? ESP_OK : ESP_ERR_INVALID_ARG;
}
