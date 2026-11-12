/* ui_diag.c — DIAG view: system info, heap, reset reason, firmware details. */
#include "board.h"
#include "diagnostics.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "input.h"
#include "lvgl.h"
#include "ui.h"
#include "ui_nav.h"
#include "wifi_mgr.h"
#include <stdatomic.h>
#include <stdio.h>

static lv_obj_t *s_key_label, *s_action_label;
static bool s_testing;
static _Atomic bool s_exporting;
static _Atomic int s_export_result;
static bool s_capture;
static void export_worker(void *arg) {
  char path[128];
  snprintf(path, sizeof(path), "/sdcard/diagnostics-%lld.log",
           (long long)esp_timer_get_time());
  if (s_capture) {
    snprintf(path, sizeof(path), "/sdcard/screenshot-%lld.bmp",
             (long long)esp_timer_get_time());
    atomic_store(&s_export_result, diagnostics_capture(path));
  } else
    atomic_store(&s_export_result, diagnostics_export_log(path));
  atomic_store(&s_exporting, false);
  vTaskDelete(NULL);
}
static void control(lv_event_t *event) {
  int action = (int)(intptr_t)lv_event_get_user_data(event);
  if (action == 0) {
    s_testing = !s_testing;
    lv_label_set_text(s_key_label,
                      s_testing ? "Teste ativo: pressione teclas; Esc encerra."
                                : "Teste encerrado");
  } else if (action == 1)
    input_kbd_reinit();
  else if (action == 2)
    wifi_retry();
  else if (action == 3)
    wifi_restart_driver();
  else if (action == 4)
    wifi_reset_c6();
  else if ((action == 5 || action == 6) &&
           !atomic_exchange(&s_exporting, true)) {
    s_capture = action == 6;
    atomic_store(&s_export_result, ESP_ERR_INVALID_STATE);
    if (xTaskCreate(export_worker, "diag_export", 6144, NULL, 2, NULL) !=
        pdPASS) {
      atomic_store(&s_export_result, ESP_ERR_NO_MEM);
      atomic_store(&s_exporting, false);
    }
  }
}
bool ui_diag_handle_key(const key_event_t *key) {
  if (!s_testing || !s_key_label)
    return false;
  if (key->kind == KEY_KIND_ESC) {
    s_testing = false;
    lv_label_set_text(s_key_label, "Teste encerrado");
    return true;
  }
  lv_label_set_text_fmt(
      s_key_label,
      "Tipo %u / Unicode U+%04lX / Ctrl %u / Shift %u / Alt %u / repetir %u",
      key->kind, (unsigned long)key_char(key), key->ctrl, key->shift, key->alt,
      key->repeat);
  return true;
}
static lv_obj_t *s_power_label;
static lv_timer_t *s_power_timer;
static void power_tick(lv_timer_t *timer) {
  if (s_action_label)
    lv_label_set_text_fmt(
        s_action_label,
        "Teclado: estado %d. Exportacao: %s (%d). LVGL: minimo livre %u bytes",
        input_kbd_state(),
        atomic_load(&s_exporting) ? "em andamento" : "ocioso",
        atomic_load(&s_export_result),
        (unsigned)uxTaskGetStackHighWaterMark(NULL));
  board_power_status_t power;
  board_power_status(&power);
  if (!power.valid) {
    lv_label_set_text(
        s_power_label,
        "INA226: sensor ausente, leitura falhou ou dados expirados");
    return;
  }
  lv_label_set_text_fmt(
      s_power_label,
      "INA226 / 0x41: %d mV  /  %d mA\nNivel estimado por tensao (2S): "
      "~%d%%\nCorrente positiva: descarga; negativa: carga. Percentual nao e "
      "medido por fuel gauge.",
      power.millivolts, power.milliamps, power.percent);
}
static void page_deleted(lv_event_t *event) {
  if (s_power_timer) {
    lv_timer_delete(s_power_timer);
    s_power_timer = NULL;
  }
  s_power_label = NULL;
  s_key_label = s_action_label = NULL;
  s_testing = false;
}

lv_obj_t *ui_diag_create(lv_obj_t *parent) {
  lv_obj_t *page = lv_obj_create(parent);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_set_style_pad_all(page, 16, 0);
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(page, 8, 0);

  lv_obj_add_event_cb(page, page_deleted, LV_EVENT_DELETE, NULL);

  /* Title */
  lv_obj_t *title = lv_label_create(page);
  lv_label_set_text(title, "Diagnostics");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xCCCCFF), 0);

  /* System info */
  char buf[512];
  size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t min_int = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

  const esp_app_desc_t *desc = esp_app_get_description();
  const esp_partition_t *running = esp_ota_get_running_partition();

  snprintf(buf, sizeof(buf),
           "App: %s  v%s\n"
           "Build: %s %s\n"
           "IDF: %s\n"
           "Partition: %s @0x%lx\n"
           "Reset: %d\n"
           "Internal free: %u KB  (min %u KB)\n"
           "PSRAM free: %u KB\n"
           "Wi-Fi: %s",
           desc ? desc->project_name : "?", desc ? desc->version : "?",
           desc ? desc->date : "?", desc ? desc->time : "?",
           esp_get_idf_version(), running ? running->label : "?",
           running ? (unsigned long)running->address : 0UL,
           (int)esp_reset_reason(), (unsigned)(free_int / 1024),
           (unsigned)(min_int / 1024), (unsigned)(free_psram / 1024),
           wifi_diagnostics());

  lv_obj_t *info = lv_label_create(page);
  lv_label_set_text(info, buf);
  lv_obj_set_style_text_color(info, lv_color_hex(0xBBBBBB), 0);
  lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
  lv_obj_set_width(info, LV_PCT(100));
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

  s_power_label = lv_label_create(page);
  lv_obj_set_width(s_power_label, LV_PCT(100));
  lv_obj_set_style_text_color(s_power_label, lv_color_hex(0xCBD5E1), 0);
  s_key_label = lv_label_create(page);
  lv_label_set_text(s_key_label, "Teste de teclado desativado");
  s_action_label = lv_label_create(page);
  lv_obj_set_width(s_action_label, LV_PCT(100));
  static const char *names[] = {"Testar teclado / encerrar",
                                "Redetectar teclado",
                                "Reconectar Wi-Fi",
                                "Reiniciar driver Wi-Fi",
                                "Reiniciar C6 (preserva senhas)",
                                "Exportar diagnostico no SD",
                                "Capturar tela BMP no SD"};
  for (unsigned i = 0; i < 7; i++) {
    lv_obj_t *b = lv_button_create(page);
    lv_obj_set_size(b, LV_PCT(100), 42);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, names[i]);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, control, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
  }
  s_power_timer = lv_timer_create(power_tick, 1000, NULL);
  power_tick(NULL);
  return page;
}
