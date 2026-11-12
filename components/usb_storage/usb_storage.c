#include "usb_storage.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_files.h"
#include "storage_guard.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "usb_host_bus.h"
#include <stdatomic.h>
#include <stdio.h>
static QueueHandle_t queue;
static SemaphoreHandle_t lock;
static usb_storage_status_t status;
static atomic_bool eject_request;
static void event(const msc_host_event_t *e, void *unused) {
  if (e->event == MSC_DEVICE_DISCONNECTED)
    sd_usb_set_available(false);
  if (xQueueSend(queue, e, 0) != pdTRUE) {
    sd_usb_set_available(false);
    atomic_store(&eject_request, true);
  }
}
static void worker(void *unused) {
  msc_host_device_handle_t device = NULL;
  msc_host_vfs_handle_t fs = NULL;
  bool closing = false;
  uint8_t pending_address = 0;
  int64_t last_space_check = 0;
  for (;;) {
    msc_host_event_t e;
    if (xQueueReceive(queue, &e, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (e.event == MSC_DEVICE_CONNECTED && !device)
        pending_address = e.device.address;
      else if (e.event == MSC_DEVICE_DISCONNECTED && device == e.device.handle)
        closing = true;
    }
    if (atomic_exchange(&eject_request, false)) {
      pending_address = 0;
      closing = true;
    }
    if (pending_address && !device && !closing && storage_io_acquire()) {
      esp_err_t err = msc_host_install_device(pending_address, &device);
      esp_vfs_fat_mount_config_t cfg = {.format_if_mount_failed = false,
                                        .max_files = 24,
                                        .allocation_unit_size = 4096};
      if (!err)
        err = msc_host_vfs_register(device, USB_FILES_ROOT, &cfg, &fs);
      xSemaphoreTake(lock, portMAX_DELAY);
      status.mounted = !err;
      status.ejecting = false;
      snprintf(status.error, sizeof(status.error), "%s",
               err ? esp_err_to_name(err) : "");
      if (!err) {
        msc_host_device_info_t info;
        if (!msc_host_get_device_info(device, &info))
          status.total = (uint64_t)info.sector_count * info.sector_size;
        sd_usb_set_available(true);
        ESP_LOGI("usb_storage", "D: montado, %llu bytes",
                 (unsigned long long)status.total);
      }
      xSemaphoreGive(lock);
      if (err)
        closing = true;
      pending_address = 0;
      storage_io_release();
    }
    if (closing) {
      sd_usb_set_available(false);
      xSemaphoreTake(lock, portMAX_DELAY);
      status.mounted = false;
      status.ejecting = true;
      xSemaphoreGive(lock);
      if (!sd_usb_active_operations()) {
        if (fs) {
          msc_host_vfs_unregister(fs);
          fs = NULL;
        }
        if (device) {
          msc_host_uninstall_device(device);
          device = NULL;
        }
        closing = false;
        xSemaphoreTake(lock, portMAX_DELAY);
        status.ejecting = false;
        status.total = status.free = 0;
        xSemaphoreGive(lock);
      }
    } else if (fs && esp_timer_get_time() - last_space_check >= 3000000 &&
               storage_io_acquire()) {
      last_space_check = esp_timer_get_time();
      uint64_t total = 0, free = 0;
      if (!esp_vfs_fat_info(USB_FILES_ROOT, &total, &free)) {
        xSemaphoreTake(lock, portMAX_DELAY);
        status.free = free;
        xSemaphoreGive(lock);
      }
      storage_io_release();
    }
  }
}
esp_err_t usb_storage_init(void) {
  if (status.started)
    return ESP_OK;
  if (!lock)
    lock = xSemaphoreCreateMutex();
  if (!lock)
    return ESP_ERR_NO_MEM;
  if (!queue)
    queue = xQueueCreate(8, sizeof(msc_host_event_t));
  if (!queue)
    return ESP_ERR_NO_MEM;
  esp_err_t e = usb_host_bus_start();
  msc_host_driver_config_t c = {.create_backround_task = true,
                                .task_priority = 5,
                                .stack_size = 4096,
                                .core_id = tskNO_AFFINITY,
                                .callback = event};
  if (!e)
    e = msc_host_install(&c);
  if (!e && xTaskCreate(worker, "usb_storage", 6144, NULL, 3, NULL) != pdPASS) {
    msc_host_uninstall();
    e = ESP_ERR_NO_MEM;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  status.started = !e;
  snprintf(status.error, sizeof(status.error), "%s",
           e ? esp_err_to_name(e) : "");
  xSemaphoreGive(lock);
  return e;
}
void usb_storage_status(usb_storage_status_t *out) {
  *out = (usb_storage_status_t){0};
  if (!lock)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  *out = status;
  xSemaphoreGive(lock);
}
esp_err_t usb_storage_eject(void) {
  if (!status.started)
    return ESP_ERR_INVALID_STATE;
  atomic_store(&eject_request, true);
  sd_usb_set_available(false);
  return ESP_OK;
}
