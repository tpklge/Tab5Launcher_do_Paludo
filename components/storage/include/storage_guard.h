#pragma once
#include <stdbool.h>
#include <stdint.h>
/* A shared lease spans the complete operation, including open files. Nested
 * leases by an existing task can drain while a mode change is quiescing. */
bool storage_io_acquire(void);
void storage_io_release(void);
bool storage_io_busy(void);
/* On success only this task may start I/O, until end or reboot. Never upgrades
 * a shared lease: callers must enter before beginning their own I/O. */
bool storage_exclusive_begin(uint32_t timeout_ms);
void storage_exclusive_end(void);
bool storage_exclusive_owned(void);
typedef struct {
  bool held;
} storage_io_scope_t;
static inline void storage_io_scope_leave(storage_io_scope_t *scope) {
  if (scope->held)
    storage_io_release();
}
#define STORAGE_IO_GUARD(error)                                                \
  storage_io_scope_t storage_io_scope __attribute__((                          \
      cleanup(storage_io_scope_leave))) = {storage_io_acquire()};              \
  if (!storage_io_scope.held)                                                  \
  return (error)
#define STORAGE_IO_GUARD_VOID()                                                \
  storage_io_scope_t storage_io_scope __attribute__((                          \
      cleanup(storage_io_scope_leave))) = {storage_io_acquire()};              \
  if (!storage_io_scope.held)                                                  \
  return

/* Mutations are serialized across UI, HTTP, serial and background workers.
 * Readers stay independent; nested writes by the same owner are allowed. */
bool storage_write_acquire(void);
void storage_write_release(void);
typedef struct {
  bool held;
} storage_write_scope_t;
static inline void storage_write_scope_leave(storage_write_scope_t *scope) {
  if (scope->held)
    storage_write_release();
}
#define STORAGE_WRITE_GUARD(error)                                             \
  storage_write_scope_t storage_write_scope __attribute__((                    \
      cleanup(storage_write_scope_leave))) = {storage_write_acquire()};        \
  if (!storage_write_scope.held)                                               \
  return (error)

/* Complete a destructive operation before its scheduled reboot: refuse new
 * work and drain every other task. A timeout keeps the reservation sealed. */
bool storage_prepare_reboot(uint32_t timeout_ms);
bool storage_reboot_required(void);
