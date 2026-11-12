#include "storage_guard.h"
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
static void take(void) { portENTER_CRITICAL(&mutex); }
static void give(void) { portEXIT_CRITICAL(&mutex); }
static uintptr_t caller(void) { return (uintptr_t)xTaskGetCurrentTaskHandle(); }
static uint64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void wait_tick(void) { vTaskDelay(pdMS_TO_TICKS(10)); }
#else
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
static atomic_flag mutex = ATOMIC_FLAG_INIT;
static _Thread_local char identity;
static void take(void) {
  while (atomic_flag_test_and_set_explicit(&mutex, memory_order_acquire))
    sched_yield();
}
static void give(void) {
  atomic_flag_clear_explicit(&mutex, memory_order_release);
}
static uintptr_t caller(void) { return (uintptr_t)&identity; }
static uint64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void wait_tick(void) {
  struct timespec t = {.tv_nsec = 1000000};
  nanosleep(&t, NULL);
}
#endif
#define OWNERS 32
static struct {
  uintptr_t task;
  unsigned depth;
} owners[OWNERS];
static uintptr_t exclusive;
static unsigned exclusive_depth, leases;
static bool reboot_required;
bool storage_io_acquire(void) {
  uintptr_t self = caller();
  take();
  int free_slot = -1;
  for (unsigned i = 0; i < OWNERS; i++) {
    if (owners[i].task == self) {
      owners[i].depth++;
      leases++;
      give();
      return true;
    }
    if (!owners[i].task)
      free_slot = i;
  }
  if (reboot_required || (exclusive && exclusive != self) || free_slot < 0) {
    give();
    return false;
  }
  owners[free_slot].task = self;
  owners[free_slot].depth = 1;
  leases++;
  give();
  return true;
}
void storage_io_release(void) {
  uintptr_t self = caller();
  take();
  for (unsigned i = 0; i < OWNERS; i++)
    if (owners[i].task == self) {
      if (owners[i].depth) {
        owners[i].depth--;
        leases--;
      }
      if (!owners[i].depth)
        owners[i].task = 0;
      break;
    }
  give();
}
bool storage_io_busy(void) {
  take();
  bool busy = reboot_required || leases || (exclusive && exclusive != caller());
  give();
  return busy;
}
bool storage_exclusive_owned(void) {
  take();
  bool owned = exclusive == caller();
  give();
  return owned;
}
bool storage_exclusive_begin(uint32_t timeout_ms) {
  uintptr_t self = caller();
  take();
  if (exclusive == self) {
    exclusive_depth++;
    give();
    return true;
  }
  if (exclusive || reboot_required) {
    give();
    return false;
  }
  for (unsigned i = 0; i < OWNERS; i++)
    if (owners[i].task == self) {
      give();
      return false;
    }
  exclusive = self;
  exclusive_depth = 1;
  give();
  uint64_t start = now_ms();
  for (;;) {
    take();
    bool drained = leases == 0;
    give();
    if (drained)
      return true;
    if (now_ms() - start >= timeout_ms) {
      storage_exclusive_end();
      return false;
    }
    wait_tick();
  }
}
void storage_exclusive_end(void) {
  take();
  if (exclusive == caller() && exclusive_depth && !--exclusive_depth)
    exclusive = 0;
  give();
}

static uintptr_t writer;
static unsigned writer_depth;
bool storage_write_acquire(void) {
  if (!storage_io_acquire())
    return false;
  uintptr_t self = caller();
  take();
  if (writer && writer != self) {
    give();
    storage_io_release();
    return false;
  }
  writer = self;
  writer_depth++;
  give();
  return true;
}
void storage_write_release(void) {
  bool released = false;
  take();
  if (writer == caller() && writer_depth) {
    released = true;
    if (!--writer_depth)
      writer = 0;
  }
  give();
  if (released)
    storage_io_release();
}

bool storage_reboot_required(void) {
  take();
  bool required = reboot_required;
  give();
  return required;
}
bool storage_prepare_reboot(uint32_t timeout_ms) {
  uintptr_t self = caller();
  take();
  reboot_required = true;
  give();
  uint64_t start = now_ms();
  for (;;) {
    take();
    unsigned own = 0;
    for (unsigned i = 0; i < OWNERS; i++)
      if (owners[i].task == self)
        own = owners[i].depth;
    bool drained = leases == own;
    give();
    if (drained)
      return true;
    if (now_ms() - start >= timeout_ms)
      return false;
    wait_tick();
  }
}
