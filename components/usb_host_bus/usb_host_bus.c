#include "usb_host_bus.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
static SemaphoreHandle_t lock;
static bool started;
static void events(void *unused) {
  for (;;) {
    uint32_t flags = 0;
    if (!usb_host_lib_handle_events(portMAX_DELAY, &flags) &&
        (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS))
      usb_host_device_free_all();
  }
}
esp_err_t usb_host_bus_start(void) {
  if (!lock)
    lock = xSemaphoreCreateMutex();
  if (!lock)
    return ESP_ERR_NO_MEM;
  xSemaphoreTake(lock, portMAX_DELAY);
  esp_err_t e = ESP_OK;
  if (!started) {
    e = board_usb5v(true);
    usb_host_config_t c = {.skip_phy_setup = false,
                           .intr_flags = ESP_INTR_FLAG_LOWMED};
    if (!e)
      e = usb_host_install(&c);
    if (!e && xTaskCreate(events, "usb_host_events", 4096, NULL, 10, NULL) !=
                  pdPASS) {
      usb_host_uninstall();
      e = ESP_ERR_NO_MEM;
    }
    if (!e)
      started = true;
  }
  xSemaphoreGive(lock);
  return e;
}
