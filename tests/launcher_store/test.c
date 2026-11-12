#include "launcher_store.h"
#include "sd_files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void copy(const char *from, const char *to) {
  FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
  assert(a && b);
  char bytes[512];
  size_t n;
  while ((n = fread(bytes, 1, sizeof(bytes), a)))
    assert(fwrite(bytes, 1, n, b) == n);
  assert(!ferror(a));
  fclose(a);
  assert(!fclose(b));
}
int main(void) {
  assert(!launcher_store_init());
  const char first[] = "sensitive configuration";
  assert(!launcher_store_write("wifi", first, sizeof(first)));
  char out[128];
  size_t n = sizeof(out);
  assert(!launcher_store_read("wifi", out, &n) && n == sizeof(first) &&
         !memcmp(out, first, n));
  assert(launcher_store_write("../escape", first, sizeof(first)) != ESP_OK);
  n = sizeof(out);
  assert(launcher_store_read("missing", out, &n) == ESP_ERR_NOT_FOUND);
  char path[512];
  snprintf(path, sizeof(path), LAUNCHER_PRIVATE "/wifi.dat");
  FILE *f = fopen(path, "r+b");
  assert(f);
  assert(!fseek(f, 12, SEEK_SET));
  assert(fputc('X', f) != EOF);
  fclose(f);
  n = sizeof(out);
  assert(launcher_store_read("wifi", out, &n) == ESP_ERR_INVALID_CRC);
  assert(!launcher_store_write("wifi", first, sizeof(first)));
  /* Simulate loss between old-file rename and publication. */
  char previous[544];
  snprintf(previous, sizeof(previous), "%s.previous", path);
  assert(!rename(path, previous));
  n = sizeof(out);
  assert(!launcher_store_read("wifi", out, &n) && !memcmp(out, first, n));
  assert(!launcher_store_write("wifi", first, sizeof(first)));
  assert(access(path, F_OK) == 0);
  /* Loss after publication but before deleting the prior committed version. */
  copy(path, previous);
  const char second[] = "next generation";
  assert(!launcher_store_write("wifi", second, sizeof(second)));
  n = sizeof(out);
  assert(!launcher_store_read("wifi", out, &n) && !strcmp(out, second));
  char archived[600];
  snprintf(archived, sizeof(archived), "%s.recovered-1", previous);
  assert(!access(archived, F_OK));
  /* Corrupt primary with a valid previous: promote, preserve corrupt bytes and
   * continue. */
  copy(path, previous);
  f = fopen(path, "r+b");
  assert(f);
  fputc(0, f);
  fclose(f);
  n = sizeof(out);
  assert(!launcher_store_read("wifi", out, &n) && !strcmp(out, second));
  assert(!launcher_store_write("wifi", first, sizeof(first)));
  snprintf(archived, sizeof(archived), "%s.recovered-1", path);
  assert(!access(archived, F_OK));
  /* A stale unfinished temporary cannot block a later durable publication. */
  char temporary[544];
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  f = fopen(temporary, "wb");
  assert(f);
  fputs("partial", f);
  fclose(f);
  assert(!launcher_store_write("wifi", second, sizeof(second)));
  assert(access(temporary, F_OK));
  n = sizeof(out);
  assert(!launcher_store_read("wifi", out, &n) && !strcmp(out, second));
  const char html[] = "<!doctype html><h1>microSD</h1>";
  snprintf(path, sizeof(path), SD_FILES_ROOT "/incoming.html");
  f = fopen(path, "wb");
  assert(f);
  fwrite(html, 1, sizeof(html) - 1, f);
  fclose(f);
  assert(!launcher_web_publish(path, "index.html"));
  assert(launcher_web_publish(path, "../config.json") == ESP_ERR_INVALID_ARG);
  f = fopen(LAUNCHER_WEB "/index.html", "rb");
  assert(f);
  n = fread(out, 1, sizeof(out), f);
  fclose(f);
  assert(n == sizeof(html) - 1 && !memcmp(out, html, n));
  puts("SD settings: CRC corruption, exact records, missing/unsafe keys, "
       "retained previous copy and raw HTML publication passed.");
}
