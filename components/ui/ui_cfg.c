#include "storage_guard.h"
#include "theme_catalog.h"
#include "usb_storage.h"
/* Settings jobs own persistence and slow hardware access; widgets stay on LVGL.
 */
#include "board.h"
#include "boot_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "installer.h"
#include "sd_files.h"
#include "storage.h"
#include "time_mgr.h"
#include "ui.h"
#include "ui_form.h"
#include "ui_nav.h"
#include "ui_picker.h"
#include "ui_visual.h"
#include "wifi_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern bool ui_sd_busy(void);
typedef enum {
  SAVE,
  TIME,
  CHARGE,
  OFF,
  FORGET,
  EXPORT_CONFIG,
  IMPORT_CONFIG,
  IMPORT_THEME
} operation_t;
typedef struct {
  operation_t op;
  unsigned generation;
  launcher_config_t config;
  time_t utc;
  bool enable;
  char path[512];
} request_t;
typedef struct {
  unsigned generation;
  operation_t op;
  esp_err_t error;
  char message[192];
} result_t;
static QueueHandle_t s_jobs, s_results;
static launcher_config_t s_edit;
static char s_config_path[512];
static unsigned s_generation;
static lv_obj_t *s_feedback;
static lv_timer_t *s_timer;
static result_t apply_request(request_t request) {
  bool attempted_off = false;
  result_t result = {.generation = request.generation, .op = request.op};
  if (request.op == SAVE) {
    result.error = storage_update(&request.config);
    if (result.error == ESP_OK)
      result.error = boot_mgr_set_timeout(request.config.boot_time_s);
    if (result.error == ESP_OK)
      board_backlight(request.config.bright);
  } else if (request.op == EXPORT_CONFIG)
    result.error = storage_export_json(request.path);
  else if (request.op == IMPORT_CONFIG)
    result.error = storage_import_json(request.path);
  else if (request.op == IMPORT_THEME)
    result.error = storage_import_theme(request.path);
  else if (request.op == TIME)
    result.error = time_set_manual(request.utc);
  else if (request.op == FORGET)
    wifi_forget_all();
  else if (installer_is_busy() || ui_sd_busy() || !ui_services_power_safe()) {
    result.error = ESP_ERR_INVALID_STATE;
    snprintf(result.message, sizeof(result.message),
             "Aguarde a operacao de arquivos/flash terminar.");
  } else if (request.op == CHARGE)
    result.error = board_charger_set(request.enable);
  else if (request.op == OFF) {
    if (!storage_exclusive_begin(1500)) {
      result.error = ESP_ERR_INVALID_STATE;
      snprintf(result.message, sizeof(result.message),
               "Ha arquivos em uso. Aguarde antes de desligar.");
      return result;
    }
    if (installer_is_busy() || ui_sd_busy() || !ui_services_power_safe()) {
      storage_exclusive_end();
      result.error = ESP_ERR_INVALID_STATE;
      return result;
    }
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
    }
    if (drive.mounted || drive.ejecting)
      result.error = ESP_ERR_INVALID_STATE;
    else {
      attempted_off = true;
      result.error = board_power_off();
    }
    storage_exclusive_end();
  }
  if (!result.message[0])
    snprintf(result.message, sizeof(result.message), "%s%s",
             result.error == ESP_OK ? "Concluido." : "Falha: ",
             result.error == ESP_OK ? "" : esp_err_to_name(result.error));
  if (request.op == TIME && result.error != ESP_OK)
    snprintf(result.message, sizeof(result.message),
             "Relogio do sistema ajustado; RTC nao confirmou gravacao: %s",
             esp_err_to_name(result.error));
  if (attempted_off)
    snprintf(
        result.message, sizeof(result.message),
        "O aparelho continua alimentado. USB pode impedir o desligamento.");
  return result;
}
static void worker(void *arg) {
  request_t request;
  while (1)
    if (xQueueReceive(s_jobs, &request, portMAX_DELAY) == pdTRUE) {
      result_t result = apply_request(request);
      xQueueOverwrite(s_results, &result);
    }
}
static bool submit(operation_t op, time_t utc, bool enable) {
  request_t r = {.op = op,
                 .generation = s_generation,
                 .config = s_edit,
                 .utc = utc,
                 .enable = enable};
  snprintf(r.path, sizeof(r.path), "%s", s_config_path);
  if (!s_jobs || xQueueSend(s_jobs, &r, 0) != pdTRUE) {
    if (s_feedback)
      lv_label_set_text(s_feedback, "Fila ocupada. Tente novamente.");
    return false;
  }
  if (s_feedback)
    lv_label_set_text(s_feedback, "Aplicando...");
  return true;
}
static void poll(lv_timer_t *timer) {
  result_t r;
  while (xQueueReceive(s_results, &r, 0) == pdTRUE)
    if (r.generation == s_generation && s_feedback) {
      lv_label_set_text(s_feedback, r.message);
      if ((r.op == IMPORT_CONFIG || r.op == IMPORT_THEME) &&
          r.error == ESP_OK) {
        ui_navigate(UI_VIEW_CFG);
        ui_visual_reload();
      }
      if (r.op == SAVE && r.error == ESP_OK)
        ui_visual_reload();
    }
}
static void deleted(lv_event_t *e) {
  ++s_generation;
  if (s_timer)
    lv_timer_delete(s_timer);
  s_timer = NULL;
  s_feedback = NULL;
  ui_form_close();
  ui_picker_close();
}
static void changed(lv_event_t *e) {
  lv_obj_t *o = lv_event_get_target(e);
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  unsigned n = 0;
  if (lv_obj_check_type(o, &lv_dropdown_class))
    n = lv_dropdown_get_selected(o);
  else if (lv_obj_check_type(o, &lv_slider_class))
    n = lv_slider_get_value(o);
  else
    n = lv_obj_has_state(o, LV_STATE_CHECKED);
  static const unsigned dim[] = {0, 30, 60, 120, 300},
                        off[] = {0, 60, 300, 600, 1800};
  switch (id) {
  case 0: {
    snprintf(s_edit.theme, sizeof(s_edit.theme), "%s", launcher_theme_name(n));
    ui_set_theme((ui_theme_t)launcher_theme_index(s_edit.theme));
    break;
  }
  case 1:
    s_edit.bright = n;
    break;
  case 2:
    s_edit.dim_s = dim[n % 5];
    break;
  case 3:
    s_edit.screen_off_s = off[n % 5];
    break;
  case 4:
    s_edit.idle_clock = n;
    break;
  case 5:
    s_edit.clock_style = n % 4;
    break;
  case 6:
    s_edit.clock_back = n % 3;
    break;
  case 7:
    s_edit.wall_dim = n % 3;
    break;
  case 8:
    s_edit.boot_time_s = n + 1;
    break;
  case 9:
    s_edit.btn_only_start = n;
    break;
  case 10:
    s_edit.deep_sleep_start = n;
    break;
  case 11:
    s_edit.wifi_autoconnect = n;
    break;
  case 13:
    s_edit.power_logging = n;
    break;
  case 14:
    s_edit.battery_warning = n;
    break;
  case 16:
    s_edit.only_bins = n;
    break;
  case 15:
    s_edit.critical_shutdown = n;
    break;
  case 12:
    submit(CHARGE, 0, n);
    return;
  }
  submit(SAVE, 0, false);
}
static bool text_save(const char *text, void *user) {
  if ((intptr_t)user >= 6 && (intptr_t)user <= 8) {
    if (!sd_path_valid(text))
      return false;
    snprintf(s_config_path, sizeof(s_config_path), "%s", text);
    return submit((intptr_t)user == 6   ? EXPORT_CONFIG
                  : (intptr_t)user == 7 ? IMPORT_CONFIG
                                        : IMPORT_THEME,
                  0, false);
  }

  int field = (int)(intptr_t)user;
  if (field == 0) {
    unsigned hex;
    char extra;
    if (strlen(text) != 6 || sscanf(text, "%6x%c", &hex, &extra) != 1)
      return false;
    for (const char *p = text; *p; p++)
      if (!strchr("0123456789abcdefABCDEF", *p))
        return false;
    snprintf(s_edit.color, sizeof(s_edit.color), "%s", text);
    ui_set_accent(hex);
  } else if (field == 1) {
    if (strlen(text) >= sizeof(s_edit.wallpaper))
      return false;
    if (text[0] && strcmp(text, "-") && !sd_path_valid(text))
      return false;
    snprintf(s_edit.wallpaper, sizeof(s_edit.wallpaper), "%s", text);
  } else if (field == 2) {
    int y, m, d, h, min, sec, n = 0;
    if (sscanf(text, "%d-%d-%d %d:%d:%d%n", &y, &m, &d, &h, &min, &sec, &n) !=
            6 ||
        text[n] || y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || min < 0 || min > 59 || sec < 0 || sec > 59)
      return false;
    struct tm t = {.tm_year = y - 1900,
                   .tm_mon = m - 1,
                   .tm_mday = d,
                   .tm_hour = h,
                   .tm_min = min,
                   .tm_sec = sec,
                   .tm_isdst = -1};
    time_t utc = mktime(&t);
    if (t.tm_year != y - 1900 || t.tm_mon != m - 1 || t.tm_mday != d ||
        t.tm_hour != h || t.tm_min != min || t.tm_sec != sec)
      return false;
    return submit(TIME, utc, false);
  } else if (field == 3) {
    if (!text[0] || strlen(text) > 31)
      return false;
    for (const char *p = text; *p; p++)
      if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567"
                  "89<>+-:,./",
                  *p))
        return false;
    snprintf(s_edit.timezone, sizeof(s_edit.timezone), "%s", text);
    snprintf(s_edit.timezone_id, sizeof(s_edit.timezone_id), "%s", text);
  } else if (field == 4) {
    if (strcmp(text, "DESLIGAR"))
      return false;
    return submit(OFF, 0, false);
  }
  if (field == 5) {
    if (strcmp(text, "ESQUECER"))
      return false;
    return submit(FORGET, 0, false);
  }
  return submit(SAVE, 0, false);
}
static bool date_confirmed(time_t value, void *user) {
  return submit(TIME, value, false);
}
static void power_confirmed(void *user) { submit(OFF, 0, false); }
static void text_open(lv_event_t *e) {
  int field = (int)(intptr_t)lv_event_get_user_data(e);
  if (field == 2) {
    ui_datetime_open(date_confirmed, NULL);
    return;
  }
  if (field == 4) {
    ui_confirm("Desligar aparelho", "Deseja desligar o Tab5?", "Desligar",
               power_confirmed, NULL);
    return;
  }
  if (field == 1) {
    ui_picker_open("Imagem de fundo", true, text_save, (void *)1);
    return;
  }
  char now[64];
  const char *initial = "", *title = "";
  unsigned max = 95;
  switch (field) {
  case 6:
  case 7:
  case 8:
    initial = field == 8 ? LAUNCHER_ROOT "/theme.json"
                         : LAUNCHER_ROOT "/config-export.json";
    title = field == 6   ? "Exportar configuracao JSON (sem senhas)"
            : field == 7 ? "Importar configuracao JSON validada"
                         : "Importar tema JSON schema 1";
    max = 511;
    break;
  case 0:
    initial = strlen(s_edit.color) == 6 ? s_edit.color : "EB2638";
    title = "COR DE DESTAQUE (6 digitos hexadecimais)";
    max = 6;
    break;
  case 1:
    initial = s_edit.wallpaper;
    title = "WALLPAPER PNG/JPEG NO SD (ate 1280x720; - desativa)";
    break;
  case 2: {
    time_t utc = time(NULL);
    struct tm t;
    localtime_r(&utc, &t);
    strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", &t);
    initial = now;
    title = "DATA E HORA LOCAL: AAAA-MM-DD HH:MM:SS";
    max = 19;
    break;
  }
  case 3:
    initial = s_edit.timezone;
    title = "FUSO POSIX (Cuiaba: <-04>4)";
    max = 31;
    break;
  case 5:
    title = "ESQUECER REDES: digite ESQUECER e confirme";
    max = 8;
    break;
  case 4:
    title = "DESLIGAR: digite DESLIGAR e confirme";
    max = 8;
    break;
  }
  ui_form_open(title, initial, false, max, text_save, (void *)(intptr_t)field);
}
static lv_obj_t *row(lv_obj_t *p, const char *text) {
  lv_obj_t *r = lv_obj_create(p);
  lv_obj_set_width(r, LV_PCT(100));
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_style_min_height(r, 86, 0);
  lv_obj_set_style_pad_all(r, 9, 0);
  lv_obj_set_style_pad_row(r, 7, 0);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *l = lv_label_create(r);
  lv_label_set_text(l, text);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
  return r;
}
static void dropdown(lv_obj_t *p, const char *name, const char *options,
                     unsigned selected, int id) {
  lv_obj_t *r = row(p, name), *d = lv_dropdown_create(r);
  lv_dropdown_set_options(d, options);
  lv_dropdown_set_selected(d, selected);
  lv_obj_set_width(d, LV_PCT(100));
  lv_obj_set_style_text_font(d, &lv_font_montserrat_18, 0);
  lv_obj_add_event_cb(d, changed, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
}
static void toggle(lv_obj_t *p, const char *name, bool value, int id) {
  lv_obj_t *r = row(p, name), *o = lv_switch_create(r);
  if (value)
    lv_obj_add_state(o, LV_STATE_CHECKED);
  lv_obj_set_size(o, 76, 36);
  lv_obj_add_event_cb(o, changed, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)id);
}
static void action(lv_obj_t *p, const char *name, int id) {
  lv_obj_t *r = row(p, name), *b = lv_button_create(r);
  lv_obj_set_size(b, LV_PCT(100), 48);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, id == 4 ? LV_SYMBOL_POWER "  Desligar" : "Abrir");
  lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
  lv_obj_center(l);
  if (id == 4)
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_3);
  lv_obj_add_event_cb(b, text_open, LV_EVENT_CLICKED, (void *)(intptr_t)id);
}
static const uint32_t colors[] = {0xEF5368, 0xF59E0B, 0xEAB308, 0x22C55E,
                                  0x14B8A6, 0x38BDF8, 0x6366F1, 0xA855F7,
                                  0xEC4899, 0xF1F5F9};
static lv_obj_t *s_swatches[10];
static void swatch(lv_event_t *e) {
  unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
  if (i >= 10)
    return;
  snprintf(s_edit.color, sizeof(s_edit.color), "%06lX",
           (unsigned long)colors[i]);
  ui_set_accent(colors[i]);
  for (unsigned j = 0; j < 10; j++)
    lv_obj_set_style_border_width(s_swatches[j], i == j ? 4 : 0, 0);
  submit(SAVE, 0, false);
}
static uint32_t custom_color(unsigned i) {
  unsigned row = i / 12, col = i % 12;
  if (row == 0) {
    unsigned g = col * 255 / 11;
    return (g << 16) | (g << 8) | g;
  }
  lv_color_t c = lv_color_hsv_to_rgb(col * 30, row <= 5 ? row * 20 : 100,
                                     row <= 5 ? 100 : 100 - (row - 5) * 14);
  return lv_color_to_u32(c) & 0xffffff;
}
static void custom_selected(lv_event_t *e) {
  uint32_t c = custom_color((unsigned)(uintptr_t)lv_event_get_user_data(e));
  snprintf(s_edit.color, sizeof(s_edit.color), "%06lX", (unsigned long)c);
  ui_set_accent(c);
  submit(SAVE, 0, false);
  ui_form_close();
}
static void custom_picker(lv_event_t *e) {
  ui_notice("COR PERSONALIZADA", "Selecione uma cor na escala 12 x 12.");
  lv_obj_t *p = ui_form_panel();
  lv_obj_set_size(p, 800, 650);
  lv_obj_t *grid = lv_obj_create(p);
  lv_obj_set_pos(grid, 70, 90);
  lv_obj_set_size(grid, 638, 466);
  lv_obj_set_style_pad_all(grid, 4, 0);
  lv_obj_set_style_pad_gap(grid, 4, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  for (unsigned i = 0; i < 144; i++) {
    lv_obj_t *b = lv_button_create(grid);
    lv_obj_set_size(b, 48, 34);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_bg_color(b, lv_color_hex(custom_color(i)), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 2, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_outline_width(b, 3, LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(b, custom_selected, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);
  }
  ui_nav_modal(p);
}
static void color_picker(lv_obj_t *p) {
  lv_obj_t *r = row(p, "Cor de destaque"), *grid = lv_obj_create(r);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(grid, 4, 0);
  lv_obj_set_style_pad_gap(grid, 10, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  unsigned selected = 0;
  sscanf(s_edit.color, "%x", &selected);
  for (unsigned i = 0; i < 10; i++) {
    lv_obj_t *b = lv_button_create(grid);
    s_swatches[i] = b;
    lv_obj_set_size(b, 44, 44);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_1);
    lv_obj_set_style_radius(b, 7, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(colors[i]), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(b, selected == colors[i] ? 4 : 0, 0);
    lv_obj_set_style_outline_width(b, 3, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(b, lv_color_hex(0xFFFFFF),
                                   LV_STATE_FOCUS_KEY);
    lv_obj_add_event_cb(b, swatch, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
  }
  lv_obj_t *b = lv_button_create(r);
  lv_obj_set_size(b, LV_PCT(100), 44);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, "Cor personalizada");
  lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, custom_picker, LV_EVENT_CLICKED, NULL);
}
static lv_obj_t *column(lv_obj_t *p, const char *name) {
  lv_obj_t *c = lv_obj_create(p);
  lv_obj_set_width(c, 0);
  lv_obj_set_flex_grow(c, 1);
  lv_obj_set_height(c, LV_PCT(100));
  lv_obj_set_style_pad_all(c, 10, 0);
  lv_obj_set_style_pad_row(c, 12, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(c, LV_DIR_VER);
  lv_obj_t *l = lv_label_create(c);
  lv_label_set_text(l, name);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_22, 0);
  return c;
}
static unsigned find(unsigned value, const unsigned *values) {
  for (unsigned i = 0; i < 5; i++)
    if (value == values[i])
      return i;
  return 0;
}
lv_obj_t *ui_cfg_create(lv_obj_t *parent) {
  ++s_generation;
  s_edit = *storage_get_config();
  if (!s_jobs) {
    s_jobs = xQueueCreate(8, sizeof(request_t));
    s_results = xQueueCreate(1, sizeof(result_t));
    if (!s_jobs || !s_results ||
        xTaskCreate(worker, "settings", 12288, NULL, 2, NULL) != pdPASS) {
      if (s_jobs)
        vQueueDelete(s_jobs);
      if (s_results)
        vQueueDelete(s_results);
      s_jobs = s_results = NULL;
      ui_notice("Configuracao", "Memoria insuficiente");
      return NULL;
    }
  }
  ui_theme_t active_theme = (ui_theme_t)launcher_theme_index(s_edit.theme);
  ui_set_theme(active_theme);
  unsigned accent;
  if (strlen(s_edit.color) == 6 && sscanf(s_edit.color, "%6x", &accent) == 1)
    ui_set_accent(accent);
  lv_obj_t *page = lv_obj_create(parent);
  lv_obj_add_flag(page, LV_OBJ_FLAG_USER_4);
  lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(page, LV_DIR_VER);
  lv_obj_set_style_pad_all(page, 20, 0);
  lv_obj_add_event_cb(page, deleted, LV_EVENT_DELETE, NULL);
  s_feedback = lv_label_create(page);
  lv_label_set_text(s_feedback,
                    "CONFIGURACOES  /  microSD: /launcher/config.json");
  lv_obj_set_width(s_feedback, LV_PCT(90));
  lv_obj_set_style_text_font(s_feedback, &lv_font_montserrat_18, 0);
  lv_obj_t *body = lv_obj_create(page);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_column(body, 14, 0);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_t *appearance = column(body, "Aparencia"),
           *screen = column(body, "Tela e relogio"),
           *system = column(body, "Sistema e conexoes");
  unsigned theme = launcher_theme_index(s_edit.theme);
  dropdown(appearance, "Tema", LAUNCHER_THEME_OPTIONS, theme, 0);
  color_picker(appearance);
  action(appearance, "Imagem de fundo", 1);
  dropdown(appearance, "Escurecer wallpaper", "Leve\nMedio\nForte",
           s_edit.wall_dim % 3, 7);
  toggle(screen, "Relogio de descanso", s_edit.idle_clock, 4);
  dropdown(screen, "Estilo do relogio", "Digital\nGrande\nDiscreto\nAnalogico",
           s_edit.clock_style % 4, 5);
  dropdown(screen, "Fundo do relogio",
           "Wallpaper desfocado\nTransparente\nPreto", s_edit.clock_back % 3,
           6);
  lv_obj_t *r = row(screen, "Brilho"), *slider = lv_slider_create(r);
  lv_obj_set_width(slider, LV_PCT(90));

  lv_slider_set_range(slider, 5, 100);
  lv_slider_set_value(slider, s_edit.bright, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, changed, LV_EVENT_RELEASED, (void *)1);
  const unsigned dim[] = {0, 30, 60, 120, 300}, off[] = {0, 60, 300, 600, 1800};
  dropdown(screen, "Reduzir brilho apos",
           "Nunca\n30 segundos\n1 minuto\n2 minutos\n5 minutos",
           find(s_edit.dim_s, dim), 2);
  dropdown(screen, "Apagar tela apos",
           "Nunca\n1 minuto\n5 minutos\n10 minutos\n30 minutos",
           find(s_edit.screen_off_s, off), 3);
  dropdown(system, "Contagem do app padrao",
           "1 segundo\n2 segundos\n3 segundos\n4 segundos\n5 segundos\n6 "
           "segundos\n7 segundos\n8 segundos\n9 segundos\n10 segundos",
           s_edit.boot_time_s >= 1 && s_edit.boot_time_s <= 10
               ? s_edit.boot_time_s - 1
               : 2,
           8);
  toggle(system, "App padrao: iniciar somente pelo botao",
         s_edit.btn_only_start, 9);
  toggle(system, "Acordar de deep sleep sempre no launcher",
         s_edit.deep_sleep_start, 10);
  action(screen, "Data/hora offline (salvar no RTC)", 2);
  action(screen, "Fuso horario", 3);
  action(system, "Exportar configuracao para SD", 6);
  action(system, "Importar configuracao do SD", 7);
  action(appearance, "Importar tema de dados JSON", 8);
  toggle(system, "Arquivos: somente .bin e pastas (ocultar ocultos)", s_edit.only_bins, 16);
  toggle(system, "Reconectar Wi-Fi ao iniciar", s_edit.wifi_autoconnect, 11);
  toggle(system, "Habilitar carga da bateria", board_charger_enabled(), 12);
  toggle(system, "Registrar energia no cartao", s_edit.power_logging, 13);
  toggle(system, "Avisar bateria baixa", s_edit.battery_warning, 14);
  toggle(system, "Desligamento critico (opcional)", s_edit.critical_shutdown,
         15);
  action(system, "Esquecer todas as redes Wi-Fi", 5);
  lv_obj_t *power = lv_button_create(page);
  lv_obj_add_flag(power, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_USER_3);
  lv_obj_set_size(power, 60, 46);
  lv_obj_align(power, LV_ALIGN_TOP_RIGHT, 0, -8);
  lv_obj_t *symbol = lv_label_create(power);
  lv_label_set_text(symbol, LV_SYMBOL_POWER);
  lv_obj_set_style_text_font(symbol, &lv_font_montserrat_24, 0);
  lv_obj_center(symbol);
  lv_obj_add_event_cb(power, text_open, LV_EVENT_CLICKED, (void *)4);
  s_timer = lv_timer_create(poll, 100, NULL);
  return page;
}
