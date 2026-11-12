#include "storage_guard.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include "sd_files.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
static int merge(const char *old, const char *dest, unsigned depth) {
  if (depth > 16)
    return ELOOP;
  struct stat st;
  if (stat(old, &st))
    return errno == ENOENT ? 0 : errno;
  if (stat(dest, &st)) {
    return rename(old, dest) ? errno : 0;
  }
  DIR *d = opendir(old);
  if (!d)
    return EEXIST;
  char *paths = malloc(SD_PATH_MAX * 2);
  if (!paths) {
    closedir(d);
    return ENOMEM;
  }
  int error = 0;
  struct dirent *entry;
  while (!error && (entry = readdir(d))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    char *a = paths, *b = paths + SD_PATH_MAX;
    if (!sd_path_join(a, SD_PATH_MAX, old, entry->d_name) ||
        !sd_path_join(b, SD_PATH_MAX, dest, entry->d_name)) {
      error = ENAMETOOLONG;
      break;
    }
    error = merge(a, b, depth + 1);
  }
  closedir(d);
  free(paths);
  if (!error && rmdir(old))
    error = errno;
  return error;
}
esp_err_t launcher_paths_migrate(void) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  const char *names[] = {"backups", "downloads", "incoming"};
  for (unsigned i = 0; i < 3; i++) {
    char old[96], dest[96];
    snprintf(old, sizeof(old), SD_FILES_ROOT "/%s", names[i]);
    snprintf(dest, sizeof(dest), LAUNCHER_ROOT "/%s", names[i]);
    int e = merge(old, dest, 0);
    if (e)
      return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}
esp_err_t launcher_adopt_image(const char *source, const char *folder,
                               char *dest, size_t size) {
  STORAGE_WRITE_GUARD(ESP_ERR_INVALID_STATE);
  if (!source || !dest || !folder ||
      (strcmp(folder, "icons") && strcmp(folder, "wall")) ||
      !sd_path_valid(source))
    return ESP_ERR_INVALID_ARG;
  if (!strncmp(source, LAUNCHER_ROOT "/", strlen(LAUNCHER_ROOT) + 1)) {
    if (strlen(source) >= size)
      return ESP_ERR_INVALID_SIZE;
    snprintf(dest, size, "%s", source);
    return ESP_OK;
  }
  if (!sd_media_acquire(source))
    return ESP_ERR_INVALID_STATE;
  FILE *f = fopen(source, "rb");
  if (!f) {
    sd_media_release(source);
    return ESP_ERR_NOT_FOUND;
  }
  struct stat st;
  uint8_t head[24];
  bool ok = !fstat(fileno(f), &st) && st.st_size >= 24 &&
            st.st_size <= 4 * 1048576 && fread(head, 1, 24, f) == 24;
  const char *ext = ok && !memcmp(head, "\x89PNG\r\n\x1a\n", 8) ? "png"
                    : ok && head[0] == 255 && head[1] == 216    ? "jpg"
                                                                : NULL;
  uint8_t *data = ext ? malloc(st.st_size) : NULL;
  ok = ext && data && !fseek(f, 0, SEEK_SET) &&
       fread(data, 1, st.st_size, f) == (size_t)st.st_size;
  fclose(f);
  sd_media_release(source);
  if (!ok) {
    free(data);
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t hash[32];
  mbedtls_sha256(data, st.st_size, hash, 0);
  char key[49];
  for (unsigned i = 0; i < 24; i++)
    snprintf(key + 2 * i, 3, "%02x", hash[i]);
  char directory[96], path[192], temp[200];
  snprintf(directory, sizeof(directory), LAUNCHER_ROOT "/%s", folder);
  mkdir(directory, 0755);
  snprintf(path, sizeof(path), "%s/%s.%s", directory, key, ext);
  if (strlen(path) >= size) {
    free(data);
    return ESP_ERR_INVALID_SIZE;
  }
  snprintf(temp, sizeof(temp), "%s.part", path);
  f = fopen(temp, "wx");
  if (!f) {
    free(data);
    return ESP_FAIL;
  }
  ok = fwrite(data, 1, st.st_size, f) == (size_t)st.st_size;
  if (fflush(f) || fsync(fileno(f)))
    ok = false;
  if (fclose(f))
    ok = false;
  if (ok) {
    f = fopen(temp, "rb");
    uint8_t actual[32];
    if (!f)
      ok = false;
    else {
      size_t n = fread(data, 1, st.st_size, f);
      ok = n == (size_t)st.st_size && !ferror(f);
      fclose(f);
      if (ok) {
        mbedtls_sha256(data, n, actual, 0);
        ok = !memcmp(actual, hash, 32);
      }
    }
  }
  if (ok && access(path, F_OK) == 0) {
    f = fopen(path, "rb");
    if (!f)
      ok = false;
    else {
      size_t n = fread(data, 1, st.st_size, f);
      uint8_t actual[32];
      ok = n == (size_t)st.st_size && fgetc(f) == EOF && !ferror(f);
      fclose(f);
      if (ok) {
        mbedtls_sha256(data, n, actual, 0);
        ok = !memcmp(actual, hash, 32);
      }
    }
  }
  free(data);
  if (ok) {
    if (access(path, F_OK) == 0)
      unlink(temp);
    else if (rename(temp, path))
      ok = false;
  }
  if (!ok) {
    unlink(temp);
    return ESP_FAIL;
  }
  snprintf(dest, size, "%s", path);
  return ESP_OK;
}
