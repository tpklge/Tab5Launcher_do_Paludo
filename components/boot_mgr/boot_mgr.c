/* boot_mgr.c — boot screen, app launch and boot-loop guard. */
#include "boot_mgr.h"
#include "board.h"
#include "boot_request.h"
#include "esp_check.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "launcher_state.h"
#include "launcher_store.h"
#include "storage.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "boot_mgr";

#define BOOT_STATE_NS "boot_mgr"
#define BOOT_STATE_STOPPED "stopped_slot"

/* SD state survives unrelated app RTC layouts; one-shot returns suppress
 * autoboot.
 */
static int s_stopped_slot = -1;
static bool s_returned;
bool boot_mgr_guard_take(boot_mgr_guard_state_t *state) {
  launcher_state_handle_t h;
  if (launcher_state_open(BOOT_STATE_NS, LAUNCHER_STATE_READWRITE, &h) !=
      ESP_OK)
    return false;
  int8_t pending = -1;
  uint8_t count = 0;
  launcher_state_get_i8(h, "pending_slot", &pending);
  if (pending < 0 || pending >= BOOT_MGR_MAX_SLOTS) {
    launcher_state_close(h);
    return false;
  }
  s_returned = true;
  char key[16];
  snprintf(key, sizeof(key), "fail_%d", pending);
  launcher_state_get_u8(h, key, &count);
  esp_reset_reason_t reason = esp_reset_reason();
  bool crash = reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
               reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
  count = crash ? (count < 255 ? count + 1 : 255) : 0;
  launcher_state_set_u8(h, key, count);
  launcher_state_erase_key(h, "pending_slot");
  launcher_state_commit(h);
  launcher_state_close(h);
  if (count < BOOT_MGR_GUARD_MAX_RESTARTS)
    return false;
  if (state)
    *state = (boot_mgr_guard_state_t){.slot = pending, .restarts = count};
  return true;
}
int boot_mgr_autoboot_slot(void) {
  const launcher_config_t *cfg = storage_get_config();
  if (s_returned || (cfg->deep_sleep_start && esp_sleep_get_wakeup_cause() !=
                                                  ESP_SLEEP_WAKEUP_UNDEFINED))
    return -1;
  int slot = boot_mgr_get_default();
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS || slot == boot_mgr_stopped_slot())
    return -1;
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
  esp_app_desc_t desc;
  return part && esp_ota_get_partition_description(part, &desc) == ESP_OK ? slot
                                                                          : -1;
}
static esp_err_t record_launch(int slot) {
  launcher_state_handle_t h;
  esp_err_t err =
      launcher_state_open(BOOT_STATE_NS, LAUNCHER_STATE_READWRITE, &h);
  if (err != ESP_OK)
    return err;
  err = launcher_state_set_i8(h, "pending_slot", slot);
  if (err == ESP_OK)
    err = launcher_state_commit(h);
  launcher_state_close(h);
  return err;
}

void boot_mgr_mark_stopped(int slot) {
  s_stopped_slot = slot;
  launcher_state_handle_t h;
  if (launcher_state_open(BOOT_STATE_NS, LAUNCHER_STATE_READWRITE, &h) ==
      ESP_OK) {
    launcher_state_set_i8(h, BOOT_STATE_STOPPED, (int8_t)slot);
    launcher_state_commit(h);
    launcher_state_close(h);
  }
}

void boot_mgr_clear_stopped(void) {
  int old_slot = boot_mgr_stopped_slot();
  s_stopped_slot = -1;
  launcher_state_handle_t h;
  if (launcher_state_open(BOOT_STATE_NS, LAUNCHER_STATE_READWRITE, &h) ==
      ESP_OK) {
    launcher_state_erase_key(h, BOOT_STATE_STOPPED);
    if (old_slot >= 0 && old_slot < BOOT_MGR_MAX_SLOTS) {
      char key[16];
      snprintf(key, sizeof(key), "fail_%d", old_slot);
      launcher_state_erase_key(h, key);
    }
    launcher_state_commit(h);
    launcher_state_close(h);
  }
}

int boot_mgr_stopped_slot(void) {
  if (s_stopped_slot != -1)
    return s_stopped_slot;
  launcher_state_handle_t h;
  if (launcher_state_open(BOOT_STATE_NS, LAUNCHER_STATE_READONLY, &h) ==
      ESP_OK) {
    int8_t v = -1;
    launcher_state_get_i8(h, BOOT_STATE_STOPPED, &v);
    launcher_state_close(h);
    s_stopped_slot = v;
  }
  return s_stopped_slot;
}

/* ---- Configuration ------------------------------------------------------- */

esp_err_t boot_mgr_set_default(int slot) {
  if (slot < -1 || slot >= BOOT_MGR_MAX_SLOTS)
    return ESP_ERR_INVALID_ARG;
  int8_t value = slot;
  return launcher_store_write("default-app", &value, sizeof(value));
}
int boot_mgr_get_default(void) {
  int8_t slot = -1;
  size_t n = sizeof(slot);
  esp_err_t e = launcher_store_read("default-app", &slot, &n);
  if (e != ESP_OK || n != sizeof(slot))
    return -1;
  return slot >= -1 && slot < BOOT_MGR_MAX_SLOTS ? slot : -1;
}
esp_err_t boot_mgr_set_timeout(uint8_t seconds) {
  if (seconds < 1 || seconds > 10)
    return ESP_ERR_INVALID_ARG;
  if (storage_get_config()->boot_time_s == seconds)
    return ESP_OK;
  launcher_config_t config = *storage_get_config();
  config.boot_time_s = seconds;
  return storage_update(&config);
}
uint8_t boot_mgr_get_timeout(void) { return storage_get_config()->boot_time_s; }

/* ---- Boot screen --------------------------------------------------------- */

void boot_mgr_start(void) {
  int default_slot = boot_mgr_get_default();
  int stopped = boot_mgr_stopped_slot();

  /* No default app, or the app was stopped by the guard: stay in launcher */
  if (default_slot < 0) {
    ESP_LOGI(TAG, "no default app, staying in launcher");
    return;
  }
  if (stopped == default_slot) {
    ESP_LOGW(TAG, "default app (slot %d) stopped by guard, staying in launcher",
             default_slot);
    return;
  }

  /* The UI component will draw the boot screen (countdown ring).
   * Here we just wait for ui_bootscreen_run() to signal its result via a
   * notification. The UI posts BOOT_ACTION_LAUNCH or BOOT_ACTION_STAY via
   * boot_mgr_bootscreen_result(). */
  ESP_LOGI(TAG, "boot screen: default=slot%d timeout=%ds", default_slot,
           boot_mgr_get_timeout());

  /* ui_bootscreen_run() is called from ui_init; it uses boot_mgr_get_default()
   * and boot_mgr_get_timeout().  When it decides to launch it calls
   * boot_mgr_launch_slot(). */
  /* This function therefore has nothing more to do; the LVGL task drives the
   * rest. */
}

/* ---- Launch -------------------------------------------------------------- */

bool boot_mgr_return_supported(void) {
  const esp_partition_t *request =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "launch_req");
  uint32_t capability = 0;
  return request && request->address == BOOT_REQUEST_OFFSET &&
         request->size == BOOT_REQUEST_SIZE &&
         esp_partition_read(request, BOOT_CAP_OFFSET, &capability,
                            sizeof(capability)) == ESP_OK &&
         capability == BOOT_CAP_MAGIC;
}

static esp_err_t (*s_prepare)(int);
static bool (*s_recovery_check)(void);
void boot_mgr_set_recovery_check(bool (*required)(void)) {
  s_recovery_check = required;
}
static void (*s_cancel_prepare)(void);
void boot_mgr_set_cancel_prepare(void (*cancel)(void)) {
  s_cancel_prepare = cancel;
}
void boot_mgr_set_prepare(esp_err_t (*prepare)(int)) { s_prepare = prepare; }

esp_err_t boot_mgr_launch_slot(int slot) {
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS)
    return ESP_ERR_INVALID_ARG;
  if (!s_recovery_check || s_recovery_check())
    return ESP_ERR_INVALID_STATE;
  ESP_LOGI(TAG, "launching slot %d", slot);

  /* Find the OTA partition for this slot index */
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP,
      (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot), NULL);
  if (!part) {
    ESP_LOGE(TAG, "slot %d partition not found", slot);
    return ESP_ERR_NOT_FOUND;
  }

  /* Verify the image before switching otadata */
  esp_partition_pos_t part_pos = {.offset = part->address, .size = part->size};
  esp_image_metadata_t meta;
  if (esp_image_verify(ESP_IMAGE_VERIFY, &part_pos, &meta) != ESP_OK) {
    ESP_LOGE(TAG, "slot %d image verification failed", slot);
    return ESP_FAIL;
  }

  if (!boot_mgr_return_supported())
    return ESP_ERR_NOT_SUPPORTED;
  if (s_prepare) {
    esp_err_t prepared = s_prepare(slot);
    if (prepared != ESP_OK)
      return prepared;
  }
  /* Recheck after preparation: it may have waited for another operation. */
  if (s_recovery_check() ||
      esp_image_verify(ESP_IMAGE_VERIFY, &part_pos, &meta) != ESP_OK) {
    if (s_cancel_prepare)
      s_cancel_prepare();
    return ESP_ERR_INVALID_STATE;
  }
  const esp_partition_t *request =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "launch_req");
  boot_request_t record = {.magic = BOOT_REQUEST_MAGIC,
                           .slot = (uint32_t)slot,
                           .address = part->address,
                           .size = part->size};
  record.crc = boot_request_crc(&record);
  esp_err_t err = esp_partition_erase_range(request, 0, request->size);
  if (err == ESP_OK)
    err = esp_partition_write(request, 0, &record, sizeof(record));
  if (err != ESP_OK) {
    if (s_cancel_prepare)
      s_cancel_prepare();
    return err;
  }

  err = record_launch(slot);
  if (err != ESP_OK) {
    uint32_t zero = 0;
    esp_partition_write(request, 0, &zero, sizeof(zero));
    if (s_cancel_prepare)
      s_cancel_prepare();
    return err;
  }

  board_shutdown();

  ESP_LOGI(TAG, "restarting into slot %d (%s)", slot, part->label);
  esp_restart();
  return ESP_OK; /* unreachable */
}

void boot_mgr_return_to_launcher(void) {
  ESP_LOGI(TAG, "return to launcher");

  const esp_partition_t *factory = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
  if (!factory) {
    ESP_LOGE(TAG, "launcher factory partition not found");
    return;
  }
  esp_err_t err = esp_ota_set_boot_partition(factory);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "cannot select launcher: %s", esp_err_to_name(err));
    return;
  }

  board_shutdown();
  esp_restart();
}

typedef struct {
  int slot;
  boot_mgr_launch_cb_t callback;
  void *user;
} launch_job_t;
static atomic_bool s_launch_pending;
static void launch_worker(void *arg) {
  launch_job_t job = *(launch_job_t *)arg;
  free(arg);
  esp_err_t err = boot_mgr_launch_slot(job.slot);
  atomic_store(&s_launch_pending, false);
  if (job.callback)
    job.callback(err, job.user);
  vTaskDelete(NULL);
}
esp_err_t boot_mgr_launch_async(int slot, boot_mgr_launch_cb_t callback,
                                void *user) {
  if (slot < 0 || slot >= BOOT_MGR_MAX_SLOTS)
    return ESP_ERR_INVALID_ARG;
  if (atomic_exchange(&s_launch_pending, true))
    return ESP_ERR_INVALID_STATE;
  launch_job_t *job = malloc(sizeof(*job));
  if (!job) {
    atomic_store(&s_launch_pending, false);
    return ESP_ERR_NO_MEM;
  }
  *job = (launch_job_t){slot, callback, user};
  if (xTaskCreate(launch_worker, "launch_app", 8192, job, 3, NULL) != pdPASS) {
    free(job);
    atomic_store(&s_launch_pending, false);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
