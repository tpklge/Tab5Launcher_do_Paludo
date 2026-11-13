#include "storage_guard.h"
#include "web_ops.h"
/* Tab5 Launcher — app_main
 *
 * Boot sequence (mirrors UNIZERSION tab5-launcher main.c, adapted for the
 * component architecture):
 *   1. Consume one-shot USB maintenance request
 *   2. diagnostics_early_init — record reset reason, coredump state
 *   3. board_init — charger, display, SD, RTC/battery (in that order)
 *   4. storage_init — configuration from SD
 *   5. time_init — RTC → system clock, timezone from config
 *   6. boot_mgr_guard_take — consume any boot-loop guard state before changing
 * anything
 *   7. input_init — Tab5 Keyboard A164 worker task (async, may be absent)
 *   8. ui_init — LVGL task + screen setup (needs board already up)
 *   9. wifi_init — ESP-Hosted async worker (non-blocking, may autoconnect from
 * saved nets)
 *  10. boot_mgr_start — decides: countdown + autoboot, or stay in launcher
 */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#include "board.h"
#include "boot_mgr.h"
#include "diagnostics.h"
#include "energy.h"
#include "flash_snapshot.h"
#include "input.h"
#include "installer.h"
#include "pi_link.h"
#include "storage.h"
#include "time_mgr.h"
#include "ui.h"
#include "ui_form.h"
#include "ui_progress.h"
#include "usb.h"
#include "usb_storage.h"
#include "web.h"
#include "wifi_mgr.h"

static const char *TAG = "launcher";
/* Keep early task-allocation failures diagnosable before app_main exists. */
static void startup_alloc_failed(size_t requested, uint32_t caps,
                                 const char *function) {
  esp_rom_printf(
      "ALLOC FAILED %s size=%u caps=%x internal=%u largest=%u\n", function,
      (unsigned)requested, (unsigned)caps,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT));
}
__attribute__((constructor)) static void startup_heap_diagnostics(void) {
  heap_caps_register_failed_alloc_callback(startup_alloc_failed);
  esp_rom_printf(
      "STARTUP heap internal=%u largest=%u\n",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT));
}

static void cancel_handoff(void) {
  storage_exclusive_end();
  usb_set_handoff_blocked(false);
  wifi_resume();
  if (board_lvgl_lock(2000)) {
    ui_progress_close();
    board_lvgl_unlock();
  }
}
static bool data_progress(unsigned percent, const char *stage, void *user) {
  if (board_lvgl_lock(100)) {
    ui_progress_update(percent, stage);
    board_lvgl_unlock();
  }
  return true;
}
static esp_err_t prepare_handoff(int slot) {
  if (installer_recovery_required())
    return ESP_ERR_INVALID_STATE;
  if (installer_is_busy() || ui_sd_busy() || ui_services_busy() ||
      usb_transfer_active() || sd_usb_active_operations())
    return ESP_ERR_INVALID_STATE;
  pi_link_status_t pi;
  pi_link_status(&pi);
  if (pi.usb) {
    ESP_LOGW(TAG, "Desconecte o Pi da USB-A antes de iniciar outro firmware "
                  "nesta revisao");
    return ESP_ERR_INVALID_STATE;
  }
  if (!storage_exclusive_begin(1500))
    return ESP_ERR_INVALID_STATE;
  if (installer_recovery_required() || installer_is_busy() || ui_sd_busy() ||
      ui_services_busy() || web_ops_busy()) {
    storage_exclusive_end();
    return ESP_ERR_INVALID_STATE;
  }
  if (!board_lvgl_lock(2000)) {
    storage_exclusive_end();
    return ESP_ERR_TIMEOUT;
  }
  ui_progress_open("Preparando aplicativo", NULL, NULL);
  board_lvgl_unlock();
  usb_set_handoff_blocked(true);
  esp_err_t result = ESP_OK;
  if (installer_is_busy() || ui_sd_busy() || ui_services_busy() ||
      usb_transfer_active() || sd_usb_active_operations())
    result = ESP_ERR_INVALID_STATE;
  if (!result) {
    usb_storage_status_t drive;
    usb_storage_status(&drive);
    if (drive.mounted || drive.ejecting) {
      usb_storage_eject();
      for (unsigned i = 0; i < 50; i++) {
        usb_storage_status(&drive);
        if (!drive.mounted && !drive.ejecting)
          break;
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (drive.mounted || drive.ejecting)
        result = ESP_ERR_TIMEOUT;
    }
  }
  if (!result) {
    web_stop();
    result = wifi_pause_wait(15000);
    if (result)
      ESP_LOGE(TAG, "Preparação do app: pausa Wi-Fi falhou: %s",
               esp_err_to_name(result));
  }
  if (!result) {
    char message[192] = {0};
    result = installer_prepare_app_data(slot, data_progress, NULL, message,
                                        sizeof(message));
    if (result)
      ESP_LOGE(TAG, "Perfil: %s",
               message[0] ? message : esp_err_to_name(result));
  }
  if (result)
    cancel_handoff();
  return result;
}

static esp_err_t navigate_view(const char *name) {
  if (installer_is_busy() || ui_sd_busy() || ui_services_busy() ||
      usb_transfer_active())
    return ESP_ERR_INVALID_STATE;
  const char *names[] = {"home",  "wifi", "sd",   "ota", "web",
                         "flash", "cfg",  "diag", "pi"};
  const ui_view_t views[] = {UI_VIEW_HOME, UI_VIEW_WIFI,  UI_VIEW_SD,
                             UI_VIEW_OTA,  UI_VIEW_WEBUI, UI_VIEW_PMAN,
                             UI_VIEW_CFG,  UI_VIEW_DIAG,  UI_VIEW_PI};
  for (unsigned i = 0; i < 9; i++)
    if (!strcmp(name, names[i])) {
      ui_navigate(views[i]);
      return ESP_OK;
    }
  return ESP_ERR_INVALID_ARG;
}
static bool energy_idle(void) {
  return !storage_io_busy() && !installer_is_busy() && !ui_sd_busy() &&
         !ui_services_busy() && !usb_transfer_active() && !web_io_busy() &&
         !sd_usb_active_operations();
}
static void energy_notice(const char *message) {
  if (board_lvgl_lock(300)) {
    ui_notice("Energia", message);
    board_lvgl_unlock();
  }
}
static bool usb_mode_allowed(void) {
  return !installer_recovery_required() && !installer_is_busy() &&
         !ui_sd_busy() && !ui_services_busy() && !web_ops_busy() &&
         !usb_transfer_active();
}
void app_main(void) {
  usb_disk_restore_console();
  /* One-shot maintenance request lives in RTC, before mounting any FAT volume. */
  esp_err_t err;
  if (usb_disk_boot_requested()) {
    usb_disk_run();
    return;
  }

  bool usb_return = usb_disk_returned();

  /* ---- 2. Early diagnostics ------------------------------------------ */
  diagnostics_early_init();

  const esp_partition_t *running = esp_ota_get_running_partition();
  ESP_LOGI(
      TAG,
      "tab5_launcher running from '%s' @0x%lx, reset=%d, free_internal=%u B",
      running ? running->label : "?",
      running ? (unsigned long)running->address : 0UL, (int)esp_reset_reason(),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  /* ---- 3. Board ------------------------------------------------------- */
  err = board_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "board_init failed: %s — halting", esp_err_to_name(err));
    /* Cannot continue without display/power management. */
    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  /* ---- 4. Storage (config) ------------------------------------------- */
  storage_init();
  wifi_saved_init(); /* loads configuration from SD; must succeed for defaults */

  /* ---- 5. Time -------------------------------------------------------- */
  installer_init();
  if (!installer_recovery_required()) {
    esp_err_t moved = launcher_paths_migrate();
    if (moved)
      ESP_LOGW(TAG,
               "Dados antigos preservados: conflito ao consolidar /launcher");
  }
  web_init();
  if (!ui_sd_prepare_worker() || !ui_services_prepare_worker())
    ESP_LOGW(TAG, "Workers de arquivos/servicos: memoria insuficiente");
  web_set_config_changed(ui_request_config_refresh);
  boot_mgr_set_prepare(prepare_handoff);
  boot_mgr_set_recovery_check(installer_recovery_required);
  boot_mgr_set_cancel_prepare(cancel_handoff);
  ESP_LOGI(TAG, "one-shot return policy: %s",
           boot_mgr_return_supported() ? "ready" : "not available");
  time_init(); /* RTC → system clock; timezone from storage config */

  /* ---- 6. Boot-loop guard -------------------------------------------- */
  boot_mgr_guard_state_t guard = {0};
  bool guarded = boot_mgr_guard_take(&guard);
  if (guarded) {
    ESP_LOGW(
        TAG,
        "app slot %d stopped by boot-loop guard: %d restarts, %d early returns",
        guard.slot, guard.restarts, guard.returns);
    boot_mgr_mark_stopped(guard.slot);
  }

  /* ---- 7. Input (Tab5 Keyboard) -------------------------------------- */
  input_init(); /* starts async worker; absent keyboard is not fatal */

  /* ---- 8. UI (LVGL) -------------------------------------------------- */
  err = ui_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ui_init failed: %s — halting", esp_err_to_name(err));
    while (1) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  if (flash_operation_pending() && board_lvgl_lock(1000)) {
    ui_notice("Flash interrompida",
              "Insira o cartão da operação e abra FLASH / Retomar. O "
              "lançamento de apps fica bloqueado até concluir.");
    board_lvgl_unlock();
  }
  if (!flash_operation_pending()) {
    if (board_lvgl_lock(1000)) {
      ui_progress_open("Verificando dados do último aplicativo", NULL, NULL);
      board_lvgl_unlock();
    }
    char data_message[192] = {0};
    esp_err_t data_error = installer_capture_profile_return(
        data_progress, NULL, data_message, sizeof(data_message));
    if (board_lvgl_lock(1000)) {
      ui_progress_close();
      if (data_error)
        ui_notice("Perfis de dados",
                  data_message[0]
                      ? data_message
                      : "Troca interrompida. Retome o mesmo aplicativo.");
      board_lvgl_unlock();
    }
  }
  /* Restore a saved network after the UI is visible, when enabled in settings.
   */
  if (storage_get_config()->wifi_autoconnect && wifi_saved_count() > 0)
    wifi_init(true);

  err = usb_storage_init();
  if (err)
    ESP_LOGW(TAG, "Pendrive USB-A: %s", esp_err_to_name(err));
  energy_init(energy_idle, energy_notice);
  usb_set_view_handler(navigate_view);
  usb_disk_set_transition_check(usb_mode_allowed);
  err = usb_init();
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Console USB indisponivel: %s", esp_err_to_name(err));

  /* ---- 9. Boot manager ---------------------------------------------- */
  char pending_path[512];
  bool pending_install =
      installer_pending_source(pending_path, sizeof(pending_path));
  if (pending_install)
    ui_navigate(UI_VIEW_SD);
  int auto_slot =
      (usb_return || pending_install || installer_recovery_required())
          ? -1
          : boot_mgr_autoboot_slot();
  if (auto_slot >= 0)
    ui_bootscreen_run(auto_slot, storage_get_config()->btn_only_start
                                     ? 0
                                     : boot_mgr_get_timeout());
  boot_mgr_start(); /* shows bootscreen; may launch default app or stay in
                       launcher */

  /* boot_mgr_start() returns only when staying in the launcher.
     The LVGL port task drives the UI from here on; this task is done. */
  ESP_LOGI(TAG, "launcher ready, heap internal=%u psram=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  vTaskDelete(NULL);
}
