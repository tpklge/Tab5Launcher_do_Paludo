/* FAT file operations. All calls are serialized by the SD UI worker. */
#include "sd_files.h"
#include "storage_guard.h"
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
static atomic_bool usb_available;
static atomic_uint usb_references;
const char *sd_path_root(const char *p) {
  if (!p)
    return NULL;
  const char *roots[] = {SD_FILES_ROOT, USB_FILES_ROOT};
  for (unsigned i = 0; i < 2; i++) {
    size_t n = strlen(roots[i]);
    if (!strncmp(p, roots[i], n) && (!p[n] || p[n] == '/'))
      return roots[i];
  }
  return NULL;
}
bool sd_media_acquire(const char *p) {
  const char *root = sd_path_root(p);
  if (!root)
    return false;
  if (!storage_io_acquire())
    return false;
  if (strcmp(root, USB_FILES_ROOT))
    return true;
  if (!atomic_load(&usb_available)) {
    storage_io_release();
    return false;
  }
  atomic_fetch_add(&usb_references, 1);
  if (!atomic_load(&usb_available)) {
    atomic_fetch_sub(&usb_references, 1);
    storage_io_release();
    return false;
  }
  return true;
}
void sd_media_release(const char *p) {
  const char *root = sd_path_root(p);
  if (root && !strcmp(root, USB_FILES_ROOT))
    atomic_fetch_sub(&usb_references, 1);
  if (root)
    storage_io_release();
}
void sd_usb_set_available(bool yes) { atomic_store(&usb_available, yes); }
unsigned sd_usb_active_operations(void) { return atomic_load(&usb_references); }
bool sd_path_valid(const char *path) {
  if (!path || strlen(path) >= SD_PATH_MAX)
    return false;
  const char *base = sd_path_root(path);
  if (!base)
    return false;
  size_t root = strlen(base);
  const char *p = path + root;
  while (*p) {
    if (*p++ != '/')
      return false;
    const char *start = p;
    while (*p && *p != '/') {
      if ((unsigned char)*p < 32 || *p == '\\')
        return false;
      p++;
    }
    size_t n = p - start;
    if (!n || n >= SD_NAME_MAX || (n == 1 && start[0] == '.') ||
        (n == 2 && start[0] == '.' && start[1] == '.'))
      return false;
  }
  return true;
}
bool sd_name_valid(const char *name) {
  if (!name || !name[0] || strlen(name) >= SD_NAME_MAX || !strcmp(name, ".") ||
      !strcmp(name, ".."))
    return false;
  size_t length = strlen(name);
  if (name[length - 1] == ' ' || name[length - 1] == '.')
    return false;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    if (*p < 32 || strchr("<>:\"/\\|?*", *p))
      return false;
  return true;
}
bool sd_path_join(char *out, size_t size, const char *dir, const char *name) {
  if (!out || !size || !sd_path_valid(dir) || !sd_name_valid(name))
    return false;
  int n = snprintf(out, size, "%s/%s", dir, name);
  return n >= 0 && (size_t)n < size && sd_path_valid(out);
}
static int order(const void *a, const void *b) {
  const sd_entry_t *x = a, *y = b;
  if (x->directory != y->directory)
    return x->directory ? -1 : 1;
  int result = strcasecmp(x->name, y->name);
  return result ? result : strcmp(x->name, y->name);
}
static int impl_sd_files_list_all(const char *path, sd_entry_t **entries,
                                  unsigned *total, bool images) {
  if (!sd_path_valid(path) || !entries || !total)
    return EINVAL;
  *entries = NULL;
  *total = 0;
  DIR *dir = opendir(path);
  if (!dir)
    return errno;
  sd_entry_t *all = NULL;
  unsigned count = 0, capacity = 0;
  int err = 0;
  struct dirent *ent;
  while ((ent = readdir(dir))) {
    if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..") ||
        !strncmp(ent->d_name, ".upload-", 8))
      continue;
    char full[SD_PATH_MAX];
    struct stat st;
    if (!sd_path_join(full, sizeof(full), path, ent->d_name) ||
        stat(full, &st) != 0)
      continue;
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))
      continue;
    const char *ext = strrchr(ent->d_name, '.');
    if (images && !S_ISDIR(st.st_mode) &&
        (!ext || (strcasecmp(ext, ".png") && strcasecmp(ext, ".jpg") &&
                  strcasecmp(ext, ".jpeg"))))
      continue;
    if (count == 16384) {
      err = E2BIG;
      break;
    }
    if (count == capacity) {
      unsigned next = capacity ? capacity * 2 : 128;
      sd_entry_t *grown = realloc(all, next * sizeof(*all));
      if (!grown) {
        err = ENOMEM;
        break;
      }
      all = grown;
      capacity = next;
    }
    sd_entry_t *e = &all[count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", ent->d_name);
    e->directory = S_ISDIR(st.st_mode);
    e->size = st.st_size;
    e->modified = st.st_mtime;
  }
  closedir(dir);
  if (!err) {
    if (count)
      qsort(all, count, sizeof(*all), order);
  }
  if (err)
    free(all);
  else {
    *entries = all;
    *total = count;
  }
  return err;
}
static int impl_sd_files_list_filtered(const char *path, unsigned page,
                                       sd_listing_t *list, const char *focus,
                                       bool images) {
  if (!list || page > 16384 / SD_PAGE_SIZE)
    return EINVAL;
  memset(list, 0, sizeof(*list));
  sd_entry_t *all = NULL;
  unsigned count = 0;
  int err = sd_files_list_all(path, &all, &count, images);
  if (err)
    return err;
  list->total = count;
  list->page = page;
  if (focus)
    for (unsigned i = 0; i < count; i++)
      if (!strcmp(all[i].name, focus)) {
        page = i / SD_PAGE_SIZE;
        list->page = page;
        break;
      }
  unsigned begin = page * SD_PAGE_SIZE;
  if (begin < count) {
    list->count = count - begin < SD_PAGE_SIZE ? count - begin : SD_PAGE_SIZE;
    memcpy(list->entries, all + begin, list->count * sizeof(*all));
  }
  free(all);
  return 0;
}
static int impl_sd_files_list(const char *p, unsigned page,
                              sd_listing_t *list) {
  return sd_files_list_filtered(p, page, list, NULL, false);
}
static int impl_sd_files_mkdir(const char *dir, const char *name) {
  char path[SD_PATH_MAX];
  if (!sd_path_join(path, sizeof(path), dir, name))
    return EINVAL;
  return mkdir(path, 0775) == 0 ? 0 : errno;
}
static int impl_sd_files_rename(const char *path, const char *name) {
  if (!sd_path_valid(path) || !strcmp(path, sd_path_root(path)))
    return EINVAL;
  char parent[SD_PATH_MAX], dest[SD_PATH_MAX];
  snprintf(parent, sizeof(parent), "%s", path);
  char *slash = strrchr(parent, '/');
  if (!slash)
    return EINVAL;
  *slash = 0;
  if (!sd_path_join(dest, sizeof(dest), parent, name))
    return EINVAL;
  if (!strcmp(path, dest))
    return 0;
  struct stat st;
  if (stat(dest, &st) == 0)
    return EEXIST;
  return rename(path, dest) == 0 ? 0 : errno;
}
static int impl_sd_files_delete(const char *path) {
  if (!sd_path_valid(path) || !strcmp(path, sd_path_root(path)))
    return EINVAL;
  struct stat st;
  if (stat(path, &st) != 0)
    return errno;
  if (S_ISDIR(st.st_mode))
    return rmdir(path) == 0 ? 0 : errno; /* only empty folders */
  return unlink(path) == 0 ? 0 : errno;
}
static int copy_file_progress(const char *source, const char *directory,
                              sd_copy_progress_fn progress, void *user) {
  if (!sd_path_valid(source) || !sd_path_valid(directory))
    return EINVAL;
  const char *name = strrchr(source, '/');
  if (!name)
    return EINVAL;
  typedef struct {
    char dest[SD_PATH_MAX], temp[SD_PATH_MAX], buffer[4096];
  } buffer_t;
  buffer_t *b = malloc(sizeof(*b));
  if (!b)
    return ENOMEM;
  FILE *in = NULL, *out = NULL;
  int err = 0;
  bool created = false;
  struct stat st;
  if (!sd_path_join(b->dest, sizeof(b->dest), directory, name + 1) ||
      snprintf(b->temp, sizeof(b->temp), "%s.part", b->dest) >=
          (int)sizeof(b->temp)) {
    err = ENAMETOOLONG;
    goto done;
  }
  if (!stat(b->dest, &st) || !stat(b->temp, &st)) {
    err = EEXIST;
    goto done;
  }
  if (stat(source, &st)) {
    err = errno;
    goto done;
  }
  if (!S_ISREG(st.st_mode)) {
    err = EISDIR;
    goto done;
  }
  in = fopen(source, "rb");
  if (!in) {
    err = errno;
    goto done;
  }
  out = fopen(b->temp, "wx");
  if (!out) {
    err = errno;
    goto done;
  }
  created = true;
  uint64_t written = 0;
  size_t n;
  while ((n = fread(b->buffer, 1, sizeof(b->buffer), in))) {
    if (fwrite(b->buffer, 1, n, out) != n) {
      err = errno ? errno : EIO;
      break;
    }
    written += n;
    if (progress &&
        !progress(st.st_size ? (unsigned)(written * 100 / st.st_size) : 100,
                  user)) {
      err = ECANCELED;
      break;
    }
  }
  if (!err && (ferror(in) || written != (uint64_t)st.st_size))
    err = EIO;
  if (fflush(out) || fsync(fileno(out)))
    err = EIO;
  if (fclose(out))
    err = EIO;
  out = NULL;
  fclose(in);
  in = NULL;
  if (!err && !stat(b->dest, &st))
    err = EEXIST;
  if (!err && rename(b->temp, b->dest))
    err = errno;
done:
  if (out)
    fclose(out);
  if (in)
    fclose(in);
  if (err && created)
    unlink(b->temp);
  free(b);
  return err;
}
static int impl_sd_files_preview(const char *path, char *text, size_t size,
                                 bool *truncated) {
  if (!sd_path_valid(path) || !text || size < 2 || !truncated)
    return EINVAL;
  FILE *file = fopen(path, "rb");
  if (!file)
    return errno;
  size_t n = fread(text, 1, size - 1, file);
  int extra = fgetc(file);
  int err = ferror(file) ? EIO : 0;
  fclose(file);
  *truncated = extra != EOF;
  for (size_t i = 0; i < n; i++) {
    unsigned char c = text[i];
    if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) {
      err = EILSEQ;
      break;
    }
  }
  text[n] = 0;
  return err;
}

static int impl_sd_files_copy(const char *source, const char *directory) {
  return sd_files_copy_progress(source, directory, NULL, NULL);
}

/* Recursion is bounded; directory copies stage a new tree and publish by
 * rename. */
static int copy_tree(const char *source, const char *target, unsigned depth,
                     unsigned *count, sd_copy_progress_fn cb, void *user) {
  if (depth > 16 || ++*count > 100000)
    return ELOOP;
  if (cb && !cb(0, user))
    return ECANCELED;
  struct stat st;
  if (stat(source, &st) != 0)
    return errno;
  if (!S_ISDIR(st.st_mode))
    return EINVAL;
  typedef struct {
    char from[SD_PATH_MAX], to[SD_PATH_MAX];
  } paths_t;
  paths_t *paths = malloc(sizeof(*paths));
  if (!paths)
    return ENOMEM;
  DIR *dir = opendir(source);
  if (!dir) {
    int error = errno;
    free(paths);
    return error;
  }
  struct dirent *e;
  int err = 0;
  while (!err && (e = readdir(dir))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char *from = paths->from, *to = paths->to;
    if (!sd_path_join(from, SD_PATH_MAX, source, e->d_name) ||
        !sd_path_join(to, SD_PATH_MAX, target, e->d_name)) {
      err = ENAMETOOLONG;
      break;
    }
    if (stat(from, &st) != 0) {
      err = errno;
      break;
    }
    if (S_ISDIR(st.st_mode)) {
      if (mkdir(to, 0775) != 0) {
        err = errno;
        break;
      }
      err = copy_tree(from, to, depth + 1, count, cb, user);
    } else if (S_ISREG(st.st_mode)) {
      if (++*count > 100000) {
        err = E2BIG;
        break;
      }
      err = copy_file_progress(from, target, cb, user);
    } else
      err = EINVAL;
  }
  closedir(dir);
  free(paths);
  return err;
}
static int impl_sd_files_copy_progress(const char *source,
                                       const char *directory,
                                       sd_copy_progress_fn cb, void *user) {
  if (!sd_path_valid(source) || !sd_path_valid(directory) ||
      !strcmp(source, sd_path_root(source)))
    return EINVAL;
  struct stat st;
  if (stat(source, &st) != 0)
    return errno;
  if (S_ISREG(st.st_mode))
    return copy_file_progress(source, directory, cb, user);
  if (!S_ISDIR(st.st_mode))
    return EINVAL;
  size_t n = strlen(source);
  if (!strncmp(source, directory, n) && (!directory[n] || directory[n] == '/'))
    return EINVAL;
  char dest[SD_PATH_MAX], temp[SD_PATH_MAX];
  if (!sd_path_join(dest, sizeof(dest), directory, strrchr(source, '/') + 1) ||
      snprintf(temp, sizeof(temp), "%s.part", dest) >= (int)sizeof(temp))
    return ENAMETOOLONG;
  if (stat(dest, &st) == 0 || stat(temp, &st) == 0)
    return EEXIST;
  if (mkdir(temp, 0775) != 0)
    return errno;
  unsigned count = 0;
  int err = copy_tree(source, temp, 0, &count, cb, user);
  if (!err && rename(temp, dest) != 0)
    err = errno;
  if (err)
    sd_files_delete_recursive(temp);
  return err;
}
static int impl_sd_files_edit(const char *path, const char *expected,
                              const char *text) {
  if (!sd_path_valid(path) || !expected || !text || strlen(text) > 4095 ||
      strlen(expected) > 4095)
    return EINVAL;
  char current[4096];
  bool truncated;
  int err = sd_files_preview(path, current, sizeof(current), &truncated);
  if (err)
    return err;
  if (truncated || strcmp(current, expected))
    return ESTALE;
  char temp[SD_PATH_MAX];
  if (snprintf(temp, sizeof(temp), "%s.edit-part", path) >= (int)sizeof(temp))
    return ENAMETOOLONG;
  struct stat st;
  if (stat(temp, &st) == 0)
    return EEXIST;
  FILE *out = fopen(temp, "wx");
  if (!out)
    return errno;
  size_t n = strlen(text);
  err = fwrite(text, 1, n, out) == n ? 0 : EIO;
  if (fflush(out) != 0 || fsync(fileno(out)) != 0)
    err = EIO;
  if (fclose(out) != 0)
    err = EIO;
  char previous[SD_PATH_MAX];
  if (snprintf(previous, sizeof(previous), "%s.previous", path) >=
      sizeof(previous))
    err = ENAMETOOLONG;
  if (!err && !stat(previous, &st))
    err = EEXIST;
  else if (!err && errno != ENOENT)
    err = errno;
  /* The exclusive staging file serializes editors of this target. Recheck the
   * version after staging so an older preview cannot replace a newer edit. */
  if (!err) {
    bool changed;
    err = sd_files_preview(path, current, sizeof(current), &changed);
    if (!err && (changed || strcmp(current, expected)))
      err = ESTALE;
  }
  bool preserved = false, published = false;
  if (!err) {
    if (rename(path, previous))
      err = errno;
    else
      preserved = true;
  }
  if (!err) {
    if (rename(temp, path))
      err = errno;
    else
      published = true;
  }
  if (!err) {
    bool changed;
    err = sd_files_preview(path, current, sizeof(current), &changed);
    if (!err && (changed || strcmp(current, text)))
      err = EIO;
  }
  if (err) {
    if (preserved) {
      if (published)
        unlink(path);
      rename(previous, path);
    }
    unlink(temp); /* This invocation created the staging file. */
  } else if (unlink(previous))
    err = errno; /* Only our own confirmed old version. */

  return err;
}

/* Preflight the complete tree before any destructive operation. Heap frames
 * avoid depth-dependent task stack use; symlinks and special files rejected. */
typedef struct {
  char path[SD_PATH_MAX];
  unsigned depth;
  DIR *dir;
} delete_frame_t;
static int tree_walk(const char *path, bool erase, unsigned *count) {
  delete_frame_t *frames = calloc(17, sizeof(*frames));
  if (!frames)
    return ENOMEM;
  snprintf(frames[0].path, SD_PATH_MAX, "%s", path);
  unsigned level = 0;
  int err = 0;
  while (!err) {
    delete_frame_t *f = &frames[level];
    struct stat st;
    if (!f->dir) {
      if (
#ifdef ESP_PLATFORM
          stat(f->path, &st)
#else
          lstat(f->path, &st)
#endif
      ) {
        err = errno;
        break;
      }
      if (++*count > 100000) {
        err = E2BIG;
        break;
      }
      if (S_ISREG(st.st_mode)) {
        if (erase && unlink(f->path))
          err = errno;
        if (!level)
          break;
        level--;
        continue;
      }
      if (!S_ISDIR(st.st_mode)) {
        err = EINVAL;
        break;
      }
      f->dir = opendir(f->path);
      if (!f->dir) {
        err = errno;
        break;
      }
    }
    errno = 0;
    struct dirent *entry = readdir(f->dir);
    if (!entry) {
      int read_error = errno;
      closedir(f->dir);
      f->dir = NULL;
      if (read_error) {
        err = read_error;
        break;
      }
      if (erase && rmdir(f->path)) {
        err = errno;
        break;
      }
      if (!level)
        break;
      level--;
      continue;
    }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;
    if (level == 16) {
      err = ELOOP;
      break;
    }
    delete_frame_t *child = &frames[level + 1];
    memset(child, 0, sizeof(*child));
    if (!sd_path_join(child->path, sizeof(child->path), f->path,
                      entry->d_name)) {
      err = ENAMETOOLONG;
      break;
    }
    level++;
  }
  for (unsigned i = 0; i < 17; i++)
    if (frames[i].dir)
      closedir(frames[i].dir);
  free(frames);
  return err;
}
static int impl_sd_files_delete_recursive(const char *path) {
  if (!sd_path_valid(path) || !strcmp(path, sd_path_root(path)))
    return EINVAL;
  unsigned count = 0;
  int err = tree_walk(path, false, &count);
  if (err)
    return err;
  count = 0;
  return tree_walk(path, true, &count);
}

int sd_files_list_all(const char *p, sd_entry_t **e, unsigned *n, bool images) {
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_list_all(p, e, n, images);
  sd_media_release(p);
  return result;
}

int sd_files_list_filtered(const char *p, unsigned page, sd_listing_t *l,
                           const char *f, bool images) {
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_list_filtered(p, page, l, f, images);
  sd_media_release(p);
  return result;
}

int sd_files_list(const char *p, unsigned page, sd_listing_t *l) {
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_list(p, page, l);
  sd_media_release(p);
  return result;
}

int sd_files_mkdir(const char *p, const char *n) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_mkdir(p, n);
  sd_media_release(p);
  return result;
}

int sd_files_rename(const char *p, const char *n) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_rename(p, n);
  sd_media_release(p);
  return result;
}

int sd_files_delete(const char *p) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_delete(p);
  sd_media_release(p);
  return result;
}

int sd_files_preview(const char *p, char *t, size_t n, bool *truncated) {
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_preview(p, t, n, truncated);
  sd_media_release(p);
  return result;
}

int sd_files_edit(const char *p, const char *expected, const char *t) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_edit(p, expected, t);
  sd_media_release(p);
  return result;
}

int sd_files_delete_recursive(const char *p) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  int result = impl_sd_files_delete_recursive(p);
  sd_media_release(p);
  return result;
}

int sd_files_copy(const char *p, const char *d) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p) || !sd_path_valid(d))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  if (!sd_media_acquire(d)) {
    sd_media_release(p);
    return ENODEV;
  }
  int result = impl_sd_files_copy(p, d);
  sd_media_release(d);
  sd_media_release(p);
  return result;
}

int sd_files_copy_progress(const char *p, const char *d, sd_copy_progress_fn cb,
                           void *u) {
  STORAGE_WRITE_GUARD(EBUSY);
  if (!sd_path_valid(p) || !sd_path_valid(d))
    return EINVAL;
  if (!sd_media_acquire(p))
    return ENODEV;
  if (!sd_media_acquire(d)) {
    sd_media_release(p);
    return ENODEV;
  }
  int result = impl_sd_files_copy_progress(p, d, cb, u);
  sd_media_release(d);
  sd_media_release(p);
  return result;
}
