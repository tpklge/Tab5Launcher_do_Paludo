/* App inventory and destructive operations run outside LVGL. */
#include "app_registry.h"
#include "board.h"
#include "boot_mgr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "installer.h"
#include "ui.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#include "ui_assets.h"
#include "ui_form.h"
#include "ui_nav.h"
#include "ui_picker.h"
#include "ui_progress.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CARD_W 420
#define CARD_H 450
#define MAX_SLOTS BOOT_MGR_MAX_SLOTS
typedef struct {
  int slot;
  bool used, valid;
  uint32_t address, size;
  char name[64], version[33], project[33];
  uint8_t digest[32], identity[32];
  app_metadata_t metadata;
  bool profile_enabled;
  app_data_config_t profile_config;
  unsigned data_count;
  installer_data_option_t data[INSTALLER_DATA_OPTIONS];
} app_t;
typedef struct {
  bool profile, capture;
  app_data_config_t profile_config;
  bool remove;
  uint32_t data_mask;
  bool backup_binary;
  unsigned generation;
  int slot;
  uint8_t digest[32];
  bool metadata, default_app;
  app_metadata_t value;
} job_t;
typedef struct {
  unsigned generation;
  esp_err_t error;
  int count, default_slot, stopped_slot;
  app_t apps[MAX_SLOTS];
  char message[256];
} result_t;
typedef struct {
  unsigned generation, percent;
  char stage[96];
} progress_t;
static QueueHandle_t s_jobs, s_results, s_progress;
static TaskHandle_t s_worker;
static atomic_bool s_busy, s_cancel;
static unsigned s_generation;
static EXT_RAM_BSS_ATTR result_t s_model;
static lv_obj_t *s_page, *s_carousel, *s_status, *s_count, *s_modal,
    *s_cards[MAX_SLOTS], *s_dots[MAX_SLOTS];
static lv_timer_t *s_poll;
static int s_active, s_remove_index;
static bool s_reload,s_rebuilding;
static void cancel_progress(void *);
static lv_obj_t *s_data_checks[INSTALLER_DATA_OPTIONS];
static uint32_t s_data_mask;
static bool s_backup_binary;
static lv_obj_t *s_backup_check;
static int s_profile_index;
static lv_obj_t *s_profile_checks[APP_DATA_MAX], *s_empty_profile;
static void render(void);
static bool progress(unsigned percent, const char *stage, void *user) {
  progress_t p = {.generation = (unsigned)(uintptr_t)user, .percent = percent};
  snprintf(p.stage, sizeof(p.stage), "%s", stage);
  xQueueOverwrite(s_progress, &p);
  return !atomic_load(&s_cancel);
}
static void inventory(result_t *r) {
  r->default_slot = boot_mgr_get_default();
  r->stopped_slot = boot_mgr_stopped_slot();
  for (int slot = 0; slot < MAX_SLOTS; slot++) {
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot, NULL);
    if (!p)
      continue; /* a hole must not hide later OTA slots */
    app_t *a = &r->apps[r->count++];
    a->slot = slot;
    a->address = p->address;
    a->size = p->size;
    uint32_t magic = 0xffffffff;
    if (esp_partition_read(p, 0, &magic, sizeof(magic)) != ESP_OK) {
      a->used = true;
      snprintf(a->name, sizeof(a->name), "Erro de leitura");
      continue;
    }
    a->used = magic != 0xffffffff;
    if (!a->used) {
      snprintf(a->name, sizeof(a->name), "Slot livre");
      continue;
    }
    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
      snprintf(a->name, sizeof(a->name), "%.32s", desc.project_name);
      snprintf(a->project, sizeof(a->project), "%.32s", desc.project_name);
      snprintf(a->version, sizeof(a->version), "%.32s", desc.version);
    } else
      snprintf(a->name, sizeof(a->name), "App incompleto");
    a->data_count = installer_data_options(slot, a->data);
    a->valid = esp_partition_get_sha256(p, a->identity) == ESP_OK;
    if (a->valid)
      a->profile_enabled = app_data_get(a->identity, &a->profile_config);
    if (a->valid && app_registry_get(a->identity, &a->metadata) &&
        a->metadata.name[0])
      memcpy(a->name, a->metadata.name, sizeof(a->name));
    if (installer_slot_digest(slot, a->digest) != ESP_OK) {
      r->error = ESP_FAIL;
      snprintf(r->message, sizeof(r->message), "Erro ao ler o slot %d", slot);
    }
  }
  ESP_LOGI("apps", "Inventory: %d OTA slots", r->count);
}
static void worker(void *arg) {
  job_t job;
  while (1) {
    if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE)
      continue;
    result_t *result = calloc(1, sizeof(*result));
    if (!result) {
      atomic_store(&s_busy, false);
      continue;
    }
    result->generation = job.generation;
    if (job.profile)
      result->error = installer_profile_configure(
          &job.profile_config, job.capture, progress,
          (void *)(uintptr_t)job.generation, result->message,
          sizeof(result->message));
    if (job.metadata)
      result->error = app_registry_set(job.digest, &job.value);
    if (job.default_app) {
      bool stopped = boot_mgr_stopped_slot() == job.slot;
      result->error =
          boot_mgr_set_default(stopped                              ? job.slot
                               : boot_mgr_get_default() == job.slot ? -1
                                                                    : job.slot);
      if (result->error == ESP_OK && stopped)
        boot_mgr_clear_stopped();
    }
    if (job.remove)
      result->error =
          installer_uninstall_options(job.slot, job.digest, job.backup_binary,job.data_mask,
                                   progress, (void *)(uintptr_t)job.generation,
                                   result->message, sizeof(result->message));
    esp_err_t operation_error = result->error;
    inventory(result);
    if (operation_error != ESP_OK)
      result->error = operation_error;
    result_t *old;
    if (xQueueReceive(s_results, &old, 0) == pdTRUE)
      free(old);
    if (xQueueSend(s_results, &result, 0) != pdTRUE)
      free(result);
    atomic_store(&s_busy, false);
  }
}
static bool start_worker(void) {
  if (s_worker)
    return true;
  if (!s_jobs)
    s_jobs = xQueueCreate(1, sizeof(job_t));
  if (!s_results)
    s_results = xQueueCreate(1, sizeof(result_t *));
  if (!s_progress)
    s_progress = xQueueCreate(1, sizeof(progress_t));
  if (!s_jobs || !s_results || !s_progress)
    return false;
  return xTaskCreate(worker, "apps_worker", 24576, NULL, 3, &s_worker) ==
         pdPASS;
}
static bool submit(bool remove, int index) {
  if (atomic_exchange(&s_busy, true))
    return false;
  job_t job = {.remove = remove, .generation = s_generation};
  if (remove) {
    job.data_mask = s_data_mask;
    job.backup_binary=s_backup_binary;
    job.slot = s_model.apps[index].slot;
    memcpy(job.digest, s_model.apps[index].digest, 32);
  }
  atomic_store(&s_cancel, false);
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
    atomic_store(&s_busy, false);
    return false;
  }
  return true;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_color(l, lv_color_hex(0xE1E6EF), 0);
  return l;
}
static void close_modal(void) {
  if (!s_modal)
    return;
  lv_obj_t *modal = s_modal;
  s_modal = NULL;
  lv_obj_delete(modal);
}
static void close_cb(lv_event_t *e) { close_modal(); }
static lv_obj_t *button(lv_obj_t *parent, const char *text, int w, int h,
                        lv_event_cb_t callback, int index) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x303640), 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0x454C58), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, (void *)(intptr_t)index);
  return b;
}
static void error_ui(void *arg) {
  lv_obj_t *toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(toast, 680, 130);
  lv_obj_center(toast);
  lv_obj_t *text = lv_label_create(toast);
  lv_obj_set_width(text, 630);
  lv_label_set_text_fmt(text, "Nao foi possivel iniciar: %s",
                        esp_err_to_name((esp_err_t)(intptr_t)arg));
  lv_obj_delete_delayed(toast, 5000);
}
static void launch_done(esp_err_t error, void *user) {
  ui_post(error_ui, (void *)(intptr_t)error);
}
static void launch_cb(lv_event_t *event) {
  int index = (int)(intptr_t)lv_event_get_user_data(event);
  esp_err_t error =
      boot_mgr_launch_async(s_model.apps[index].slot, launch_done, NULL);
  if (error != ESP_OK)
    error_ui((void *)(intptr_t)error);
}
static bool metadata_save(const char *value, void *user) {
  int code = (int)(intptr_t)user, index = code / 2;
  bool icon = code & 1;
  if (index < 0 || index >= s_model.count || atomic_exchange(&s_busy, true))
    return false;
  job_t job = {.metadata = true,
               .generation = s_generation,
               .value = s_model.apps[index].metadata};
  memcpy(job.digest, s_model.apps[index].identity, 32);
  if (icon) {
    if (value[0] && !sd_path_valid(value)) {
      atomic_store(&s_busy, false);
      return false;
    }
    snprintf(job.value.icon, sizeof(job.value.icon), "%s", value);
  } else {
    if (!value[0] || strlen(value) > 63) {
      atomic_store(&s_busy, false);
      return false;
    }
    snprintf(job.value.name, sizeof(job.value.name), "%s", value);
  }
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
    atomic_store(&s_busy, false);
    return false;
  }
  return true;
}
static void rename_app(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (atomic_load(&s_busy))
    return;
  ui_form_open("NOME DO APLICATIVO", s_model.apps[i].name, false, 63,
               metadata_save, (void *)(intptr_t)(2 * i));
}
static void profile_save(lv_event_t *e) {
  if (atomic_exchange(&s_busy, true))
    return;
  app_t *a = &s_model.apps[s_profile_index];
  job_t job = {.profile = true,
               .generation = s_generation,
               .capture = !lv_obj_has_state(s_empty_profile, LV_STATE_CHECKED)};
  memcpy(job.profile_config.identity, a->identity, 32);
  for (unsigned i = 0; i < a->data_count; i++)
    if (lv_obj_has_state(s_profile_checks[i], LV_STATE_CHECKED)) {
      const esp_partition_t *p = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, a->data[i].label);
      if (!p)
        continue;
      app_data_region_t *r =
          &job.profile_config.regions[job.profile_config.count++];
      snprintf(r->label, sizeof(r->label), "%s", p->label);
      r->address = p->address;
      r->size = p->size;
      r->nvs = !strcmp(p->label, "nvs");
    }
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
    atomic_store(&s_busy, false);
    return;
  }
  close_modal();
  ui_progress_open("Salvando perfil de dados", cancel_progress, NULL);
}
static void profile_open(lv_event_t *e) {
  if (atomic_load(&s_busy))
    return;
  s_profile_index = (int)(intptr_t)lv_event_get_user_data(e);
  app_t *a = &s_model.apps[s_profile_index];
  close_modal();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  ui_nav_modal(s_modal);
  lv_obj_add_event_cb(s_modal, close_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_t *p = lv_obj_create(s_modal);
  lv_obj_set_size(p, 1000, 630);
  lv_obj_center(p);
  lv_obj_t *l = label(p, "PERFIL DE DADOS POR APLICATIVO", 0, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  l = label(p,
            "Selecione apenas os dados usados por este app. Sem seleção: "
            "compartilhado.\nNVS troca os namespaces dos apps e preserva os "
            "registros do launcher.\nSalvar captura os dados atuais como "
            "perfil deste aplicativo.",
            0, 45);
  lv_obj_set_width(l, 930);
  lv_obj_t *box = lv_obj_create(p);
  lv_obj_set_pos(box, 0, 145);
  lv_obj_set_size(box, 930, 300);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  for (unsigned i = 0; i < a->data_count; i++) {
    lv_obj_t *c = lv_checkbox_create(box);
    s_profile_checks[i] = c;
    char text[160];
    snprintf(text, sizeof(text), "%s / %lu KiB%s", a->data[i].label,
             (unsigned long)a->data[i].size / 1024,
             !strcmp(a->data[i].label, "nvs") ? " / namespaces" : "");
    lv_checkbox_set_text(c, text);
    for (unsigned j = 0; j < a->profile_config.count; j++)
      if (!strcmp(a->profile_config.regions[j].label, a->data[i].label))
        lv_obj_add_state(c, LV_STATE_CHECKED);
  }
  s_empty_profile = lv_checkbox_create(p);
  lv_obj_set_pos(s_empty_profile, 0, 458);
  lv_checkbox_set_text(s_empty_profile,
                       "Perfil apagado: o app deve inicializar FAT/SPIFFS");
  lv_obj_t *b = button(p, "Cancelar", 180, 48, close_cb, 0);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  b = button(p, "Salvar perfil", 240, 48, profile_save, 0);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  ui_nav_refresh();
  ui_nav_focus(b);
}
static void icon_app(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (atomic_load(&s_busy))
    return;
  ui_picker_open("Ícone do aplicativo", false, metadata_save,
                 (void *)(intptr_t)(2 * i + 1));
}
static void default_app(lv_event_t *e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (atomic_exchange(&s_busy, true))
    return;
  job_t job = {.default_app = true,
               .generation = s_generation,
               .slot = s_model.apps[i].slot};
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE)
    atomic_store(&s_busy, false);
}
static void sd_cb(lv_event_t *event) { ui_navigate(UI_VIEW_SD); }
static void cancel_progress(void *arg) { atomic_store(&s_cancel, true); }
static void confirm_remove(lv_event_t *event) {
  s_backup_binary=lv_obj_has_state(s_backup_check,LV_STATE_CHECKED);
  s_data_mask = 0;
  for (unsigned i = 0; i < s_model.apps[s_remove_index].data_count; i++)
    if (lv_obj_has_state(s_data_checks[i], LV_STATE_CHECKED))
      s_data_mask |= 1u << i;
  close_modal();
  ui_progress_open("DESINSTALANDO APLICATIVO", cancel_progress, NULL);
  if (!submit(true, s_remove_index)) {
    ui_progress_close();
    lv_label_set_text(s_status, "Outra operacao esta em andamento.");
  }
}
static void remove_cb(lv_event_t *event) {
  int index = (int)(intptr_t)lv_event_get_user_data(event);
  if (atomic_load(&s_busy) || !s_model.apps[index].used)
    return;
  s_remove_index = index;
  close_modal();
  app_t *app = &s_model.apps[index];
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_modal, 0, 0);
  lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x080A0F), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
  ui_nav_modal(s_modal);
  lv_obj_add_event_cb(s_modal, close_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_t *panel = lv_obj_create(s_modal);
  lv_obj_set_size(panel, 940, 630);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x20242B), 0);
  lv_obj_set_style_pad_all(panel, 24, 0);
  lv_obj_t *title = label(panel, "DESINSTALAR APLICATIVO", 0, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_t *info = label(panel, "", 0, 60);
  lv_obj_set_width(info, 880);
  lv_label_set_text_fmt(
      info,
      "Remover %s (slot %d)? Backup do aplicativo opcional.\n"
      "Escolha dados adicionais para backup (não serão apagados).\n"
      "Um .bin isolado não informa quais dados pertencem ao app.\n"
      "A etapa final de remoção não pode ser cancelada.",
      app->name, app->slot);
  lv_obj_t *data_box = lv_obj_create(panel);
  lv_obj_set_pos(data_box, 0, 190);
  lv_obj_set_size(data_box, 880, 300);
  lv_obj_set_flex_flow(data_box, LV_FLEX_FLOW_COLUMN);
  s_backup_check=lv_checkbox_create(data_box);lv_checkbox_set_text(s_backup_check,"Salvar backup do aplicativo antes de remover");lv_obj_add_state(s_backup_check,LV_STATE_CHECKED);
  for (unsigned i = 0; i < app->data_count; i++) {
    lv_obj_t *c = lv_checkbox_create(data_box);
    s_data_checks[i] = c;
    char text[160];
    snprintf(text, sizeof(text), "%s / %lu KiB / %s", app->data[i].label,
             (unsigned long)app->data[i].size / 1024,
             app->data[i].associated ? "associada pelo pacote"
                                     : "possivelmente compartilhada");
    lv_checkbox_set_text(c, text);
    if (app->data[i].associated)
      lv_obj_add_state(c, LV_STATE_CHECKED);
  }
  lv_obj_t *cancel = button(panel, "Cancelar", 180, 48, close_cb, 0);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *yes = button(panel, "Desinstalar", 190, 48, confirm_remove, 0);
  lv_obj_align(yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_add_flag(yes, LV_OBJ_FLAG_USER_3);
  lv_obj_set_style_bg_color(yes, lv_color_hex(0xB6172C), 0);
  ui_nav_refresh();
  ui_nav_focus(cancel);
}
static void choose(int index) {
  if (s_rebuilding || index < 0 || index >= s_model.count)
    return;
  s_active = index;
  for (int i = 0; i < s_model.count; i++) {
    if (s_cards[i]) {
      if (i == index)
        lv_obj_add_state(s_cards[i], LV_STATE_CHECKED);
      else
        lv_obj_clear_state(s_cards[i], LV_STATE_CHECKED);
    }
    if (s_cards[i])
      lv_obj_set_style_border_color(
          s_cards[i], lv_color_hex(i == index ? 0xEB2638 : 0x454C58), 0);
    if (s_dots[i])
      lv_obj_set_style_bg_color(
          s_dots[i], lv_color_hex(i == index ? 0xEB2638 : 0x454C58), 0);
  }
}
static void card_focused(lv_event_t *event) {
  choose((int)(intptr_t)lv_event_get_user_data(event));
}
static void scroll_ended(lv_event_t *event) {
  if (s_rebuilding || !s_carousel || !s_model.count)
    return;
  lv_area_t area;
  lv_obj_get_coords(s_carousel, &area);
  int center = (area.x1 + area.x2) / 2, best = 0, distance = INT32_MAX;
  for (int i = 0; i < s_model.count; i++)
    if (s_cards[i]) {
      lv_obj_get_coords(s_cards[i], &area);
      int d = abs((area.x1 + area.x2) / 2 - center);
      if (d < distance) {
        distance = d;
        best = i;
      }
    }
  choose(best);
}
static void icon_ready(lv_event_t *e) {
  lv_obj_t *fallback = lv_event_get_user_data(e);
  if (lv_obj_is_valid(fallback))
    lv_obj_add_flag(fallback, LV_OBJ_FLAG_HIDDEN);
}
static void render(void) {
  s_rebuilding=true;
  memset(s_cards, 0, sizeof(s_cards));
  memset(s_dots, 0, sizeof(s_dots));
  lv_obj_clean(s_carousel);
  int used = 0;
  for (int i = 0; i < s_model.count; i++) {
    app_t *a = &s_model.apps[i];
    if (a->used)
      ++used;
    lv_obj_t *card = lv_obj_create(s_carousel);
    s_cards[i] = card;
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x20242B), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x454C58), 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *slot = label(card, "", 10, 8);
    lv_label_set_text_fmt(slot, "SLOT %d  /  %u MB", a->slot,
                          (unsigned)(a->size / (1024 * 1024)));
    lv_obj_t *icon =
        label(card, a->used ? LV_SYMBOL_FILE : LV_SYMBOL_PLUS, 0, 28);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_32, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t *name = label(card, a->name, 8, 112);
    lv_obj_set_width(name, 350);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_t *version =
        label(card, a->used ? a->version : "Instale pelo menu SD", 8, 154);
    lv_obj_set_style_text_color(version, lv_color_hex(0x9CA6B5), 0);
    lv_obj_t *start = button(card, a->used ? "INICIAR APP" : "INSTALAR DO SD",
                             350, 48, a->used ? launch_cb : sd_cb, i);
    lv_obj_add_flag(start, LV_OBJ_FLAG_USER_2);
    lv_obj_align(start, LV_ALIGN_BOTTOM_MID, 0, a->used ? -164 : -20);
    lv_obj_set_style_bg_color(start, lv_color_hex(0xB6172C), 0);
    if (a->used && !a->valid)
      lv_obj_add_state(start, LV_STATE_DISABLED);
    lv_obj_add_event_cb(start, card_focused, LV_EVENT_FOCUSED,
                        (void *)(intptr_t)i);
    if (a->used) {
      const char *icon_path = a->metadata.icon;
      char fallback[512];
      if (!icon_path[0]) {
        snprintf(fallback, sizeof(fallback), SD_FILES_ROOT "/apps/%s/icon.png",
                 a->name);
        if (sd_path_valid(fallback))
          icon_path = fallback;
      }
      if (icon_path[0] && sd_path_valid(icon_path)) {
        lv_obj_t *image = lv_image_create(card);
        lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_add_event_cb(image, icon_ready, LV_EVENT_READY, icon);
        ui_assets_set(image, icon_path, 256, 256, 64, 64);
      }
      lv_obj_t *rename = button(card, "Renomear", 168, 38, rename_app, i);
      lv_obj_align(rename, LV_ALIGN_BOTTOM_LEFT, 0, -116);
      if (!a->valid)
        lv_obj_add_state(rename, LV_STATE_DISABLED);
      lv_obj_t *def = button(card,
                             s_model.default_slot == a->slot ? "Tirar padrao"
                                                             : "Usar no boot",
                             168, 38, default_app, i);
      lv_obj_align(def, LV_ALIGN_BOTTOM_RIGHT, 0, -116);
      if (!a->valid)
        lv_obj_add_state(def, LV_STATE_DISABLED);
      lv_obj_t *icon_button = button(card, "Ícone", 168, 38, icon_app, i);
      lv_obj_align(icon_button, LV_ALIGN_BOTTOM_LEFT, 0, -68);
      lv_obj_t *data_button = button(
          card,
          a->profile_enabled && a->profile_config.count ? "Dados " LV_SYMBOL_OK
                                                        : "Dados do app",
          168, 38, profile_open, i);
      lv_obj_align(data_button, LV_ALIGN_BOTTOM_RIGHT, 0, -68);
      if (atomic_load(&s_busy))
        lv_obj_add_state(data_button, LV_STATE_DISABLED);
      if (!a->valid)
        lv_obj_add_state(icon_button, LV_STATE_DISABLED);
      lv_obj_t *remove = button(card, "Desinstalar", 350, 42, remove_cb, i);
      lv_obj_add_flag(remove, LV_OBJ_FLAG_USER_3);
      lv_obj_align(remove, LV_ALIGN_BOTTOM_MID, 0, -20);
      lv_obj_add_event_cb(remove, card_focused, LV_EVENT_FOCUSED,
                          (void *)(intptr_t)i);
    }
  }
  lv_label_set_text_fmt(s_count,
                        "%d app(s) instalado(s)  /  %d slots. Instalacoes "
                        "ocupam slots livres antes de substituir.",
                        used, s_model.count);
  if (s_active >= s_model.count)
    s_active = 0;
  s_rebuilding=false;
  choose(s_active);
  ui_style_refresh();
  ui_nav_refresh();
  if (s_model.count)
    lv_obj_scroll_to_view(s_cards[s_active], LV_ANIM_OFF);
}
static void poll(lv_timer_t *timer) {
  progress_t p;
  if (xQueueReceive(s_progress, &p, 0) == pdTRUE &&
      p.generation == s_generation) {
    ui_progress_update(p.percent, p.stage);
    if (strstr(p.stage, "Removendo slot"))
      ui_progress_set_cancelable(false);
  }
  result_t *r;
  while (xQueueReceive(s_results, &r, 0) == pdTRUE) {
    if (r->generation == s_generation) {
      ui_progress_close();
      if (r->count || !r->error) {
        s_model = *r;
        render();
      }
      if (r->error)
        lv_label_set_text(
            s_status, r->message[0]
                          ? r->message
                          : "Falha na operacao. Confira o slot e o backup.");
      else
        lv_label_set_text(
            s_status, r->message[0]
                          ? r->message
                          : "Setas/Tab navegam, Enter ativa, Esc/F6 volta ao "
                            "menu. Segure a rolagem para ver outros apps.");
    }
    free(r);
  }
  if (s_reload && !atomic_load(&s_busy)) {
    s_reload = false;
    submit(false, 0);
  }
}
static void deleted(lv_event_t *event) {
  if (s_poll) {
    lv_timer_delete(s_poll);
    s_poll = NULL;
  }
  close_modal();
  ui_form_close();
  ui_picker_close();
  ui_progress_close();
  ++s_generation;
  s_page = s_carousel = s_status = s_count = NULL;
  memset(s_cards, 0, sizeof(s_cards));
}
lv_obj_t *ui_home_create(lv_obj_t *parent) {
  ++s_generation;
  s_reload = false;
  s_page = lv_obj_create(parent);
  lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(s_page, 0, 0);
  lv_obj_set_style_border_width(s_page, 0, 0);
  lv_obj_set_style_bg_opa(s_page, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_page, deleted, LV_EVENT_DELETE, NULL);
  lv_obj_update_layout(s_page);
  lv_obj_t *title = label(s_page, "APPS NA FLASH", 28, 24);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  s_count = label(s_page, "Lendo aplicativos...", 28, 60);
  s_carousel = lv_obj_create(s_page);
  lv_obj_set_pos(s_carousel, 0, 94);
  lv_obj_set_size(s_carousel, LV_PCT(100),
                  lv_obj_get_content_height(s_page) - 146);
  lv_obj_set_style_bg_opa(s_carousel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_carousel, 0, 0);
  lv_obj_set_flex_flow(s_carousel, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(s_carousel, 24, 0);
  int padding = (lv_obj_get_content_width(s_page) - CARD_W) / 2;
  lv_obj_set_style_pad_left(s_carousel, padding > 0 ? padding : 0, 0);
  lv_obj_set_style_pad_right(s_carousel, padding > 0 ? padding : 0, 0);
  lv_obj_set_flex_align(s_carousel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scroll_snap_x(s_carousel, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scroll_dir(s_carousel, LV_DIR_HOR);
  lv_obj_add_event_cb(s_carousel, scroll_ended, LV_EVENT_SCROLL_END, NULL);
  s_status = label(s_page, "Verificando slots...", 28, 0);
  lv_obj_set_width(s_status, LV_PCT(94));
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
  lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 28, -18);
  if (start_worker()) {
    s_poll = lv_timer_create(poll, 80, NULL);
    if (!submit(false, 0)) {
      s_reload = true;
      lv_label_set_text(s_status, "Aguardando operacao anterior...");
    }
  } else
    lv_label_set_text(s_status, "Memoria insuficiente para ler os apps");
  return s_page;
}
