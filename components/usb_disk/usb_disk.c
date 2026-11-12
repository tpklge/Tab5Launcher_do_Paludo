#include "storage_guard.h"
#include "usb_storage.h"
/* Dedicated, one-shot maintenance boot. No launcher FAT users coexist with MSC.
 */
#include "board.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/usb_wrap_ll.h"
#include "sdmmc_cmd.h"
#include "soc/usb_wrap_struct.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "usb_disk.h"
#include <stdatomic.h>
void usb_disk_restore_console(void) { usb_wrap_ll_phy_select(&USB_WRAP, 1); }
static RTC_NOINIT_ATTR volatile struct {
  uint32_t magic, phase, seconds;
} last_mode;
void usb_disk_last_status(uint32_t *phase, uint32_t *seconds) {
  *phase = last_mode.magic == 0x4d534330 ? (last_mode.phase & 255u) : 0;
  *seconds = last_mode.magic == 0x4d534330 ? last_mode.seconds : 0;
}
bool usb_disk_returned(void) {
  if (last_mode.magic != 0x4d534330 || !last_mode.phase ||
      (last_mode.phase & 0x80000000u))
    return false;
  last_mode.phase |= 0x80000000u;
  return true;
}
/* Consumed once on a software restart; never initializes or writes flash KV. */
#define USB_REQUEST_MAGIC 0x31515355u
static RTC_NOINIT_ATTR volatile struct {
  uint32_t magic, inverse;
} mode_request;
static esp_err_t persist_request(void) {
  mode_request.magic = 0;
  mode_request.inverse = ~USB_REQUEST_MAGIC;
  atomic_thread_fence(memory_order_seq_cst);
  mode_request.magic = USB_REQUEST_MAGIC;
  return ESP_OK;
}
static bool (*transition_allowed)(void);
static atomic_bool mode_requested;
static atomic_int mode_error;
void usb_disk_set_transition_check(bool (*allowed)(void)) {
  transition_allowed = allowed;
}
esp_err_t usb_disk_transition_error(void) { return atomic_load(&mode_error); }
static void mode_worker(void *unused) {
  vTaskDelay(pdMS_TO_TICKS(
      500)); /* Give the accepted HTTP/serial ACK time to leave. */
  esp_err_t e = ESP_ERR_INVALID_STATE;
  bool reserved = storage_exclusive_begin(2000);
  if (reserved && transition_allowed && transition_allowed()) {
    usb_storage_eject();
    usb_storage_status_t drive;
    for (unsigned i = 0; i < 50; i++) {
      usb_storage_status(&drive);
      if (!drive.mounted && !drive.ejecting)
        break;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    usb_storage_status(&drive);
    if (!drive.mounted && !drive.ejecting && transition_allowed()) {
      e = persist_request();
      if (!e)
        usb_disk_restart();
    }
  }
  if (reserved)
    storage_exclusive_end();
  atomic_store(&mode_error, e);
  atomic_store(&mode_requested, false);
  ESP_LOGE("usb_disk", "Troca USB nao iniciada: %s", esp_err_to_name(e));
  vTaskDelete(NULL);
}
esp_err_t usb_disk_request(void) {
  if (!transition_allowed || !transition_allowed() || storage_io_busy() ||
      atomic_exchange(&mode_requested, true))
    return ESP_ERR_INVALID_STATE;
  atomic_store(&mode_error, ESP_OK);
  /* No boot flag is persisted unless the worker exists and all I/O drained. */
  if (xTaskCreate(mode_worker, "usb_mode", 4096, NULL, 4, NULL) != pdPASS) {
    atomic_store(&mode_requested, false);
    atomic_store(&mode_error, ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
bool usb_disk_boot_requested(void) {
  bool requested = esp_reset_reason() == ESP_RST_SW &&
                   mode_request.magic == USB_REQUEST_MAGIC &&
                   mode_request.inverse == ~USB_REQUEST_MAGIC;
  mode_request.magic = 0;
  mode_request.inverse = 0;
  return requested;
}
static bool msc_active;
void usb_disk_restart(void) {
  if (msc_active) {
    /* Let the host retire MSC before routing the pads back to Serial/JTAG. */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    tinyusb_driver_uninstall();
    msc_active = false;
  }
  usb_disk_restore_console();
  board_display_guard_restart();
  esp_restart();
}
static lv_obj_t *disk_status, *disk_confirm;
static _Atomic bool exit_requested;
static void exit_confirmed(lv_event_t *e) {
  atomic_store(&exit_requested, true);
}
static void cancel_exit(lv_event_t *e) {
  lv_obj_delete(disk_confirm);
  disk_confirm = NULL;
}
static void disk_exit_dialog(lv_event_t *e) {
  if (disk_confirm)
    return;
  disk_confirm = lv_obj_create(lv_layer_top());
  lv_obj_set_size(disk_confirm, 850, 250);
  lv_obj_center(disk_confirm);
  lv_obj_set_style_pad_all(disk_confirm, 24, 0);
  lv_obj_t *label = lv_label_create(disk_confirm);
  lv_label_set_text(label, "Ejete o microSD no computador antes de sair.\nA "
                           "ejecao ja foi concluida?");
  lv_obj_set_width(label, 790);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_t *yes = lv_button_create(disk_confirm);
  lv_obj_set_size(yes, 340, 60);
  lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_add_event_cb(yes, exit_confirmed, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(yes);
  lv_label_set_text(label, "Sim, voltar ao launcher");
  lv_obj_center(label);
  lv_obj_t *no = lv_button_create(disk_confirm);
  lv_obj_set_size(no, 260, 60);
  lv_obj_align(no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_event_cb(no, cancel_exit, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(no);
  lv_label_set_text(label, "Cancelar");
  lv_obj_center(label);
}
static void disk_screen(void) {
  if (board_display_init() || !board_lvgl_lock(2000))
    return;
  lv_obj_t *root = lv_screen_active();
  lv_obj_set_style_bg_color(root, lv_color_hex(0x11151c), 0);
  lv_obj_set_style_text_color(root, lv_color_hex(0xeef3fc), 0);
  lv_obj_set_style_text_font(root, &lv_font_montserrat_24, 0);
  lv_obj_t *title = lv_label_create(root);
  lv_label_set_text(title, "USB - CARTAO microSD");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 130);
  disk_status = lv_label_create(root);
  lv_obj_set_width(disk_status, 1000);
  lv_label_set_text(disk_status, "Preparando cartao para o computador...");
  lv_obj_set_style_text_align(disk_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(disk_status, LV_ALIGN_CENTER, 0, -40);
  lv_obj_t *label = lv_label_create(root);
  lv_label_set_text(
      label, "O launcher esta pausado. Ejete no computador antes de sair.");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 70);
  lv_obj_t *button = lv_button_create(root);
  lv_obj_set_size(button, 420, 70);
  lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -100);
  lv_obj_add_event_cb(button, disk_exit_dialog, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(button);
  lv_label_set_text(label, "Voltar apos ejetar");
  lv_obj_center(label);
  board_lvgl_unlock();
  board_reveal(65);
}
void usb_disk_run(void) {
  last_mode.magic = 0x4d534330;
  last_mode.phase = 1;
  last_mode.seconds = 0;
  last_mode.phase = 2;
  sdmmc_card_t *card = NULL;
  esp_err_t e = board_sd_export_card(&card);
  if (!e) {
    last_mode.phase = 3;
    tinyusb_msc_driver_config_t driver = {.user_flags.auto_mount_off = 1};
    e = tinyusb_msc_install_driver(&driver);
  }
  if (!e) {
    tinyusb_msc_storage_config_t disk = {.medium.card = card,
                                         .mount_point =
                                             TINYUSB_MSC_STORAGE_MOUNT_USB,
                                         .fat_fs = {.do_not_format = true}};
    e = tinyusb_msc_new_storage_sdmmc(&disk, NULL);
  }
  if (!e)
    last_mode.phase = 4;
  usb_phy_handle_t phy = NULL;
  if (!e) {
    usb_phy_config_t cfg = {.controller = USB_PHY_CTRL_OTG,
                            .target = USB_PHY_TARGET_INT,
                            .otg_mode = USB_OTG_MODE_DEVICE,
                            .otg_speed = USB_PHY_SPEED_FULL};
    e = usb_new_phy(&cfg, &phy);
  }
  if (!e) { /* Tab5 USB-C uses PHY0 (GPIO24/25), PHY1 is used by audio. */
    usb_wrap_ll_phy_select(&USB_WRAP, 0);
    last_mode.phase = 5;
    tinyusb_config_t cfg = TINYUSB_CONFIG_FULL_SPEED(NULL, NULL);
    cfg.phy.skip_setup = true;
    e = tinyusb_driver_install(&cfg);
  }
  if (e) {
    ESP_LOGE("usb_disk", "MSC: %s; retornando ao launcher", esp_err_to_name(e));
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
  }
  /* Eject in the computer before RESET. The flag was already consumed, so
   * RESET always returns to normal launcher and its Serial/JTAG console. */
  msc_active = true;
  last_mode.phase = 6;
  /* Enumerate first; the display does not delay the host seeing the disk. */
  disk_screen();
  unsigned tick = 0;
  while (1) {
    if (atomic_load(&exit_requested))
      usb_disk_restart();
    if (tick++ % 20) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    last_mode.seconds++;
    if (tud_mounted())
      last_mode.phase = 7;
    if (disk_status && board_lvgl_lock(100)) {
      lv_label_set_text(
          disk_status,
          tud_mounted()
              ? "Cartao disponivel no computador.\nPode copiar arquivos e "
                "pastas."
              : "Aguardando o computador liberar o acessorio USB.\nSe houver "
                "um aviso no computador, permita o acesso.");
      board_lvgl_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/* Optional host exit after the OS has ejected/unmounted the filesystem. */
bool tud_vendor_control_xfer_cb(uint8_t port, uint8_t stage,
                                const tusb_control_request_t *request) {
  if (request->bmRequestType != 0x40 || request->bRequest != 0x5a ||
      request->wValue != 0x5442 || request->wIndex || request->wLength)
    return false;
  if (stage == CONTROL_STAGE_SETUP)
    return tud_control_status(port, request);
  if (stage == CONTROL_STAGE_ACK)
    atomic_store(&exit_requested, true);
  return true;
}
