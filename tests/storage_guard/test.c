#include "sd_files.h"
#include "storage_guard.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static atomic_bool ready, release_owner, exclusive_result, probe_done,
    probe_acquired, files_closed;
static void nap(void) {
  struct timespec t = {.tv_nsec = 1000000};
  nanosleep(&t, NULL);
}
static void *probe(void *unused) {
  bool ok = storage_io_acquire();
  atomic_store(&probe_acquired, ok);
  if (ok)
    storage_io_release();
  atomic_store(&probe_done, true);
  return NULL;
}
static void *write_probe(void *unused) {
  bool ok = storage_write_acquire();
  atomic_store(&probe_acquired, ok);
  if (ok)
    storage_write_release();
  return NULL;
}
static bool other_can_write(void) {
  pthread_t t;
  assert(!pthread_create(&t, NULL, write_probe, NULL));
  assert(!pthread_join(t, NULL));
  return atomic_load(&probe_acquired);
}
static bool other_can_acquire(void) {
  pthread_t t;
  atomic_store(&probe_done, false);
  assert(!pthread_create(&t, NULL, probe, NULL));
  assert(!pthread_join(t, NULL));
  assert(atomic_load(&probe_done));
  return atomic_load(&probe_acquired);
}
static void *exclusive_wait(void *unused) {
  atomic_store(&ready, true);
  bool ok = storage_exclusive_begin(1000);
  atomic_store(&exclusive_result, ok);
  if (ok) {
    assert(atomic_load(&files_closed));
    assert(storage_exclusive_owned());
    assert(!storage_io_busy());
    assert(storage_io_acquire());
    assert(storage_io_busy());
    storage_io_release();
    while (!atomic_load(&release_owner))
      nap();
    storage_exclusive_end();
  }
  return NULL;
}
static void *timeout_wait(void *unused) {
  bool ok = storage_exclusive_begin(30);
  assert(!ok);
  assert(!storage_exclusive_owned());
  return NULL;
}
int main(void) {
  assert(!storage_io_busy());
  assert(storage_write_acquire());
  assert(storage_write_acquire());
  assert(other_can_acquire());
  assert(!other_can_write());
  storage_write_release();
  assert(!other_can_write());
  storage_write_release();
  assert(other_can_write());
  assert(storage_io_acquire());
  FILE *file = tmpfile();
  assert(file && fputs("durable before mode switch", file) >= 0);
  assert(!storage_exclusive_begin(0));
  pthread_t mode;
  assert(!pthread_create(&mode, NULL, exclusive_wait, NULL));
  while (!atomic_load(&ready))
    nap();
  bool denied = false;
  for (unsigned i = 0; i < 300; i++) {
    if (!other_can_acquire()) {
      denied = true;
      break;
    }
    nap();
  }
  assert(denied);
  assert(!atomic_load(&exclusive_result));
  /* A writer already draining can finish nested filesystem calls. */
  assert(storage_io_acquire());
  storage_io_release();
  assert(storage_io_busy());
  assert(!fflush(file) && !fsync(fileno(file)) && !fclose(file));
  atomic_store(&files_closed, true);
  storage_io_release();
  for (unsigned i = 0; i < 1000 && !atomic_load(&exclusive_result); i++)
    nap();
  assert(atomic_load(&exclusive_result));
  assert(!other_can_acquire());
  atomic_store(&release_owner, true);
  assert(!pthread_join(mode, NULL));
  assert(other_can_acquire());
  assert(!storage_io_busy());
  assert(storage_io_acquire());
  pthread_t t;
  assert(!pthread_create(&t, NULL, timeout_wait, NULL));
  assert(!pthread_join(t, NULL));
  assert(other_can_acquire());
  storage_io_release();
  assert(storage_exclusive_begin(0));
  assert(storage_exclusive_begin(0));
  storage_exclusive_end();
  assert(storage_exclusive_owned());
  storage_exclusive_end();
  assert(!storage_io_busy());
  assert(storage_write_acquire());
  assert(storage_prepare_reboot(0));
  assert(storage_reboot_required());
  assert(!other_can_acquire() && !other_can_write());
  assert(storage_io_acquire());
  storage_io_release();
  storage_write_release();
  assert(storage_io_busy() && !storage_exclusive_begin(0));
  puts("Storage coordinator: real threads, writer drain, blocked new work, "
       "nested completion, owner I/O, no upgrade deadlock, exclusive nesting "
       "and timeout rollback passed.");
}
