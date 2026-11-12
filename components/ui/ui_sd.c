#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "flash_snapshot.h"
/* SD browser: LVGL-only view and one serialized worker for file/flash
 * operations. */
#include "board.h"
#include "boot_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "installer.h"
#include "sd_files.h"
#include "ui.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#include "esp_log.h"
#include "esp_system.h"
#include "ui_form.h"
#include "ui_nav.h"
#include "ui_progress.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

typedef enum {
  JOB_LIST,
  JOB_MKDIR,
  JOB_RENAME,
  JOB_DELETE,
  JOB_COPY,
  JOB_PREVIEW,
  JOB_ANALYZE,
  JOB_INSTALL,
  JOB_FULL_PLAN,
  JOB_FULL_RESTORE,
  JOB_EDIT_LOAD,
  JOB_EDIT_SAVE,
  JOB_CANCEL_PENDING
} job_t;
typedef struct {
  job_t type;
  unsigned generation, page;
  char path[SD_PATH_MAX], name[SD_NAME_MAX], extra[SD_PATH_MAX], edited[4096],
      expected[4096];
  install_plan_t plan;
} request_t;
typedef struct {
  job_t type;
  unsigned generation;
  int error;
  char message[256], text[4097], path[SD_PATH_MAX];
  bool truncated;
  sd_listing_t listing;
  install_plan_t plan;
} result_t;
typedef struct {
  unsigned generation, percent;
  char stage[96];
} progress_t;
static QueueHandle_t s_jobs, s_results, s_progress;
static TaskHandle_t s_worker;
static atomic_bool s_busy, s_cancel;
static atomic_int s_operation;
static unsigned s_generation, s_page_index;
static bool s_reload, s_long_press;
static EXT_RAM_BSS_ATTR sd_listing_t s_listing;
static flash_snapshot_t *s_full_snapshot;
static char s_open_source[SD_PATH_MAX];
static char s_cwd[SD_PATH_MAX] = SD_FILES_ROOT, s_clipboard[SD_PATH_MAX];
static int s_selected = -1;
static EXT_RAM_BSS_ATTR install_plan_t s_plan;

static lv_obj_t *s_page, *s_list, *s_status, *s_path, *s_details, *s_prev,
    *s_next, *s_paste;
static lv_obj_t *s_rename, *s_delete, *s_copy, *s_open, *s_analyze, *s_edit;
static lv_obj_t *s_modal, *s_field, *s_keyboard, *s_modal_hint, *s_accept,
    *s_install;
static lv_timer_t *s_poll;
static job_t s_dialog_job;
static EXT_RAM_BSS_ATTR char s_editor_original[4096],
    s_editor_path[SD_PATH_MAX];
static bool start_worker(void);
bool ui_sd_prepare_worker(void) { return start_worker(); }

static bool submit(job_t type, const char *path, const char *name,
                   const char *extra);
static void render_list(void);
static void cancel_progress(void *unused) { atomic_store(&s_cancel, true); }
static void update_selection(void);

static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y,
                      uint32_t color) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, value);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}
static lv_obj_t *button(lv_obj_t *parent, const char *caption, int w, int h,
                        lv_event_cb_t callback) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x303640), 0);
  lv_obj_set_style_bg_grad_color(b, lv_color_hex(0x242830), 0);
  lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(0x454C58), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x581C26), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(b, lv_color_hex(0xEB2638), LV_STATE_CHECKED);
  lv_obj_t *label = lv_label_create(b);
  lv_label_set_text(label, caption);
  lv_obj_center(label);
  if (callback)
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, NULL);
  return b;
}
static void enable(lv_obj_t *obj, bool yes) {
  if (yes)
    lv_obj_clear_state(obj, LV_STATE_DISABLED);
  else
    lv_obj_add_state(obj, LV_STATE_DISABLED);
}
static void human_size(uint64_t size, char *out, size_t n) {
  if (size >= 1024 * 1024)
    snprintf(out, n, "%.2f MB", (double)size / (1024 * 1024));
  else if (size >= 1024)
    snprintf(out, n, "%.1f KB", (double)size / 1024);
  else
    snprintf(out, n, "%llu B", (unsigned long long)size);
}
static bool selected_path(char path[SD_PATH_MAX]) {
  return s_selected >= 0 && s_selected < (int)s_listing.count &&
         sd_path_join(path, SD_PATH_MAX, s_cwd,
                      s_listing.entries[s_selected].name);
}
static bool progress(unsigned percent, const char *stage, void *user) {
  progress_t p = {.generation = (unsigned)(uintptr_t)user, .percent = percent};
  snprintf(p.stage, sizeof(p.stage), "%s", stage);
  xQueueOverwrite(s_progress, &p);
  return !atomic_load(&s_cancel);
}
static bool copy_progress(unsigned percent, void *user) {
  vTaskDelay(1);
  return progress(percent, "Copiando arquivo", user);
}
static void *payload_alloc(size_t size) {
#ifdef ESP_PLATFORM
  return heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return calloc(1, size);
#endif
}
static void worker(void *unused) {
  request_t *request;
  while (1) {
    if (xQueueReceive(s_jobs, &request, portMAX_DELAY) != pdTRUE)
      continue;
    result_t *r = payload_alloc(sizeof(*r));
    if (!r) {
      free(request);
      atomic_store(&s_busy, false);
      continue;
    }
    r->type = request->type;
    snprintf(r->path, sizeof(r->path), "%s", request->path);
    r->generation = request->generation;
    if (sd_path_root(request->path) &&
        !strcmp(sd_path_root(request->path), SD_FILES_ROOT) &&
        !board_sd_ready())
      board_sd_mount();
    switch (request->type) {
    case JOB_LIST:
      r->error = sd_files_list(request->path, request->page, &r->listing);
      break;
    case JOB_MKDIR:
      r->error = sd_files_mkdir(request->path, request->name);
      break;
    case JOB_RENAME:
      r->error = sd_files_rename(request->path, request->name);
      break;
    case JOB_DELETE:
      r->error = sd_files_delete_recursive(request->path);
      break;
    case JOB_COPY:
      r->error =
          sd_files_copy_progress(request->path, request->extra, copy_progress,
                                 (void *)(uintptr_t)request->generation);
      break;
    case JOB_CANCEL_PENDING:
      r->error = installer_cancel_pending(request->path);
      break;
    case JOB_EDIT_SAVE:
      r->error =
          sd_files_edit(request->path, request->expected, request->edited);
      break;
    case JOB_EDIT_LOAD:
    case JOB_PREVIEW:
      r->error = sd_files_preview(request->path, r->text, sizeof(r->text),
                                  &r->truncated);
      break;
    case JOB_FULL_PLAN:
      if (!s_full_snapshot)
        s_full_snapshot = calloc(1, sizeof(*s_full_snapshot));
      r->error = s_full_snapshot
                     ? flash_snapshot_validate(request->path, s_full_snapshot,
                                               r->message, sizeof(r->message))
                     : ESP_ERR_NO_MEM;
      break;
    case JOB_FULL_RESTORE:
      r->error = installer_full_restore(s_full_snapshot, progress,
                                        (void *)(uintptr_t)request->generation,
                                        r->message, sizeof(r->message));
      if (!r->error)
        esp_restart();
      break;
    case JOB_ANALYZE:
      r->error = installer_plan(request->path, &r->plan, r->message,
                                sizeof(r->message));
      break;
    case JOB_INSTALL:
      r->error = installer_execute(&request->plan, progress,
                                   (void *)(uintptr_t)request->generation,
                                   r->message, sizeof(r->message));
      break;
    }
    if (r->error && !r->message[0]) {
      if (request->type <= JOB_PREVIEW)
        snprintf(r->message, sizeof(r->message), "Falha: %s (%d)",
                 strerror(r->error), r->error);
      else
        snprintf(r->message, sizeof(r->message), "Falha: %s",
                 esp_err_to_name(r->error));
    }
    free(request);
    result_t *old;
    if (xQueueReceive(s_results, &old, 0) == pdTRUE)
      free(old);
    if (xQueueSend(s_results, &r, 0) != pdTRUE)
      free(r);
    atomic_store(&s_busy, false);
  }
}
static bool start_worker(void) {
  if (s_worker)
    return true;
  if (!s_jobs)
    s_jobs = xQueueCreate(1, sizeof(request_t *));
  if (!s_results)
    s_results = xQueueCreate(1, sizeof(result_t *));
  if (!s_progress)
    s_progress = xQueueCreate(1, sizeof(progress_t));
  if (!s_jobs || !s_results || !s_progress)
    return false;
  return xTaskCreate(worker, "sd_worker", 16384, NULL, 3, &s_worker) == pdPASS;
}
static bool submit(job_t type, const char *path, const char *name,
                   const char *extra) {
  if (!s_worker || atomic_exchange(&s_busy, true)) {
    if (s_status)
      lv_label_set_text(s_status, "Uma operacao esta em andamento. Aguarde.");
    if (type == JOB_LIST)
      s_reload = true;
    return false;
  }
  atomic_store(&s_cancel, false);
  atomic_store(&s_operation, type);
  /* This payload includes editor buffers and an install plan (~13 KB). It
   * stays in PSRAM. The queue transfers ownership of its pointer to the worker.
   */
  request_t *request = payload_alloc(sizeof(*request));
  if (!request) {
    atomic_store(&s_busy, false);
    if (s_status)
      lv_label_set_text(
          s_status, "Sem memoria para iniciar a operacao. Tente novamente.");
    return false;
  }
  request->type = type;
  request->generation = s_generation;
  request->page = s_page_index;
  if (path)
    snprintf(request->path, sizeof(request->path), "%s", path);
  if (name)
    snprintf(request->name, sizeof(request->name), "%s", name);
  if (extra && type != JOB_EDIT_SAVE)
    snprintf(request->extra, sizeof(request->extra), "%s", extra);
  if (type == JOB_EDIT_SAVE) {
    snprintf(request->edited, sizeof(request->edited), "%s",
             extra ? extra : "");
    snprintf(request->expected, sizeof(request->expected), "%s",
             s_editor_original);
  }
  if (type == JOB_INSTALL)
    request->plan = s_plan;
  bool queued = xQueueSend(s_jobs, &request, 0) == pdTRUE;
  if (!queued)
    free(request);
  if (!queued) {
    atomic_store(&s_busy, false);
    return false;
  }
  if (s_status)
    lv_label_set_text(s_status, type == JOB_INSTALL
                                    ? "Instalacao em andamento..."
                                    : "Aguarde...");
  return true;
}
static void close_modal(void) {
  if (!s_modal)
    return;
  lv_obj_t *modal = s_modal;
  s_modal = s_field = s_keyboard = s_modal_hint = s_accept = s_install = NULL;
  lv_obj_delete(modal);
}
static void close_cb(lv_event_t *e) { close_modal(); }
static lv_obj_t *modal_panel(const char *title, int height) {
  close_modal();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  ui_nav_modal(s_modal);
  lv_obj_add_event_cb(s_modal, close_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_set_pos(s_modal, 0, 0);
  lv_obj_set_style_pad_all(s_modal, 0, 0);
  lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x06080C), 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
  lv_obj_set_style_border_width(s_modal, 0, 0);
  lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *panel = lv_obj_create(s_modal);
  lv_obj_set_size(panel, 850, height);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_set_style_pad_all(panel, 22, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x20242B), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0xEB2638), 0);
  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *heading = text(panel, title, 0, 0, 0xFFFFFF);
  lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
  return panel;
}
static void keyboard_hide(lv_event_t *e) {
  if (s_keyboard)
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void keyboard_show(lv_event_t *e) {
  if (s_keyboard)
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void dialog_confirm(lv_event_t *e) {
  char path[SD_PATH_MAX];
  if (s_dialog_job == JOB_MKDIR) {
    const char *name = lv_textarea_get_text(s_field);
    if (!sd_name_valid(name)) {
      lv_label_set_text(
          s_modal_hint,
          "Nome invalido. Use um nome sem /, \\, : ou pontos finais.");
      return;
    }
    submit(JOB_MKDIR, s_cwd, name, NULL);
  } else if (s_dialog_job == JOB_RENAME) {
    const char *name = lv_textarea_get_text(s_field);
    if (!sd_name_valid(name)) {
      lv_label_set_text(s_modal_hint, "Nome invalido");
      return;
    }
    if (selected_path(path))
      submit(JOB_RENAME, path, name, NULL);
  } else if (s_dialog_job == JOB_DELETE && selected_path(path))
    submit(JOB_DELETE, path, NULL, NULL);
  close_modal();
}
static void name_dialog(job_t job) {
  if (atomic_load(&s_busy))
    return;
  s_dialog_job = job;
  lv_obj_t *panel =
      modal_panel(job == JOB_MKDIR ? "NOVA PASTA" : "RENOMEAR", 330);
  s_field = lv_textarea_create(panel);
  lv_obj_set_pos(s_field, 0, 54);
  lv_obj_set_size(s_field, 800, 56);
  lv_obj_set_style_bg_color(s_field, lv_color_hex(0x14171C), 0);
  lv_obj_set_style_text_color(s_field, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(s_field, &lv_font_montserrat_24, 0);
  lv_textarea_set_one_line(s_field, true);
  lv_textarea_set_max_length(s_field, SD_NAME_MAX - 1);
  if (job == JOB_RENAME && s_selected >= 0)
    lv_textarea_set_text(s_field, s_listing.entries[s_selected].name);
  s_modal_hint =
      text(panel, "Enter confirma  /  Esc cancela", 0, 128, 0x9CA6B5);
  lv_obj_t *kb =
      button(panel, LV_SYMBOL_KEYBOARD " Teclado", 160, 42, keyboard_show);
  lv_obj_set_pos(kb, 0, 164);
  lv_obj_t *cancel = button(panel, "Cancelar", 150, 48, close_cb);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *ok = button(panel, "Confirmar", 170, 48, dialog_confirm);
  lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  s_keyboard = lv_keyboard_create(s_modal);
  lv_obj_set_size(s_keyboard, LV_PCT(100), 300);
  lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(s_keyboard, s_field);
  lv_obj_add_event_cb(s_keyboard, keyboard_hide, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_keyboard, keyboard_hide, LV_EVENT_CANCEL, NULL);
  if (input_kbd_present())
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  ui_nav_refresh();
  ui_nav_focus(s_field);
}
static void mkdir_cb(lv_event_t *e) { name_dialog(JOB_MKDIR); }
static void rename_cb(lv_event_t *e) {
  if (s_selected >= 0)
    name_dialog(JOB_RENAME);
}
static void delete_cb(lv_event_t *e) {
  if (s_selected < 0 || atomic_load(&s_busy))
    return;
  s_dialog_job = JOB_DELETE;
  lv_obj_t *panel = modal_panel("EXCLUIR DO CARTAO", 280);
  s_modal_hint = text(panel, "", 0, 56, 0xFFFFFF);
  lv_obj_set_width(s_modal_hint, 790);
  lv_label_set_text_fmt(s_modal_hint,
                        "Excluir %s?\n\nA acao remove o arquivo ou a pasta e "
                        "todo seu conteudo. Nao pode ser desfeita.",
                        s_listing.entries[s_selected].name);
  lv_obj_t *cancel = button(panel, "Cancelar", 160, 48, close_cb);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_t *confirm = button(panel, "Excluir", 170, 48, dialog_confirm);
  lv_obj_add_flag(confirm, LV_OBJ_FLAG_USER_3);
  lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(confirm, lv_color_hex(0xB6172C), 0);
}
static void copy_cb(lv_event_t *e) {
  if (selected_path(s_clipboard)) {
    enable(s_paste, true);
    lv_label_set_text(
        s_status,
        "Arquivo selecionado para copia. Abra o destino e toque em Colar.");
  }
}
static void paste_cb(lv_event_t *e) {
  if (s_clipboard[0])
    submit(JOB_COPY, s_clipboard, NULL, s_cwd);
}
static void drive_cb(lv_event_t *e) {
  if (atomic_load(&s_busy))
    return;
  snprintf(s_cwd, sizeof(s_cwd), "%s",
           lv_event_get_user_data(e) ? USB_FILES_ROOT : SD_FILES_ROOT);
  s_page_index = 0;
  submit(JOB_LIST, s_cwd, NULL, NULL);
}
static void refresh_cb(lv_event_t *e) { submit(JOB_LIST, s_cwd, NULL, NULL); }
static void up_cb(lv_event_t *e) {
  if (!strcmp(s_cwd, sd_path_root(s_cwd)) || atomic_load(&s_busy))
    return;
  char *slash = strrchr(s_cwd, '/');
  if (slash)
    *slash = 0;
  s_page_index = 0;
  submit(JOB_LIST, s_cwd, NULL, NULL);
}
static void prev_cb(lv_event_t *e) {
  if (s_page_index && !atomic_load(&s_busy)) {
    --s_page_index;
    submit(JOB_LIST, s_cwd, NULL, NULL);
  }
}
static void next_cb(lv_event_t *e) {
  if (!atomic_load(&s_busy) &&
      (s_page_index + 1) * SD_PAGE_SIZE < s_listing.total) {
    ++s_page_index;
    submit(JOB_LIST, s_cwd, NULL, NULL);
  }
}
static void pressed_cb(lv_event_t *e) { s_long_press = false; }
static void long_cb(lv_event_t *e) {
  if (atomic_load(&s_busy))
    return;
  s_long_press = true;
  s_selected = (int)(intptr_t)lv_event_get_user_data(e);
  update_selection();
}
static void select_cb(lv_event_t *e) {
  if (s_long_press) {
    s_long_press = false;
    return;
  }
  if (atomic_load(&s_busy))
    return;
  int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index < 0 || index >= (int)s_listing.count)
    return;
  s_selected = index;
  if (s_listing.entries[index].directory) {
    char path[SD_PATH_MAX];
    if (!selected_path(path))
      return;
    snprintf(s_cwd, sizeof(s_cwd), "%s", path);
    s_page_index = 0;
    submit(JOB_LIST, s_cwd, NULL, NULL);
  } else
    update_selection();
}
static void preview_cb(lv_event_t *e) {
  char path[SD_PATH_MAX];
  if (selected_path(path))
    submit(JOB_PREVIEW, path, NULL, NULL);
}
static bool save_editor(const char *value, void *arg) {
  return submit(JOB_EDIT_SAVE, s_editor_path, NULL, value);
}
static void edit_cb(lv_event_t *e) {
  char path[SD_PATH_MAX];
  if (selected_path(path))
    submit(JOB_EDIT_LOAD, path, NULL, NULL);
}
static void full_confirmed(void *arg) {
  ui_progress_open("Restaurando flash integral", NULL, NULL);
  submit(JOB_FULL_RESTORE, NULL, NULL, NULL);
}
static void analyze_cb(lv_event_t *e) {
  char path[SD_PATH_MAX];
  if (selected_path(path)) {
    const char *ext = strrchr(path, '.');
    submit(ext && !strcasecmp(ext, ".full") ? JOB_FULL_PLAN : JOB_ANALYZE, path,
           NULL, NULL);
  }
}
static void cancel_operation(lv_event_t *e) {
  int op = atomic_load(&s_operation);
  if (!atomic_load(&s_busy) || (op != JOB_INSTALL && op != JOB_COPY))
    return;
  atomic_store(&s_cancel, true);
  if (s_status)
    lv_label_set_text(s_status,
                      "Cancelamento solicitado no proximo ponto seguro.");
}
static void install_confirm(lv_event_t *e) {
  if (!s_accept || !lv_obj_has_state(s_accept, LV_STATE_CHECKED))
    return;
  close_modal();
  ui_progress_open("INSTALANDO APLICATIVO", cancel_progress, NULL);
  if (!submit(JOB_INSTALL, s_plan.path, NULL, NULL))
    ui_progress_close();
}
static void accept_cb(lv_event_t *e) {
  if (s_install)
    enable(s_install, lv_obj_has_state(s_accept, LV_STATE_CHECKED));
}
static void launch_error_ui(void *arg) {
  esp_err_t err = (esp_err_t)(intptr_t)arg;
  if (!err)
    return;
  char message[160];
  snprintf(message, sizeof(message), "Não foi possível iniciar: %s",
           esp_err_to_name(err));
  ui_notice("Iniciar aplicativo", message);
}
static void launch_done(esp_err_t error, void *user) {
  ui_post(launch_error_ui, (void *)(intptr_t)error);
}
static void launch_cb(lv_event_t *e) {
  if (atomic_load(&s_busy))
    return;
  int slot = s_plan.slot;
  close_modal();
  esp_err_t error = boot_mgr_launch_async(slot, launch_done, NULL);
  if (error)
    launch_error_ui((void *)(intptr_t)error);
}
static void cancel_plan(lv_event_t *e) {
  close_modal();
  submit(JOB_CANCEL_PENDING, s_plan.path, NULL, NULL);
}
static void show_plan(void) {
  lv_obj_t *panel = modal_panel("PLANO DE INSTALACAO", 590);
  char size[32], capacity[32];
  human_size(s_plan.firmware.image.image_size, size, sizeof(size));
  human_size(s_plan.size, capacity, sizeof(capacity));
  lv_obj_t *info = text(panel, "", 0, 54, 0xDCE2EB);
  lv_obj_set_width(info, 790);
  const char *kinds[] = {
      "App-only", "Merged (bootloader/tabela de origem nao serao gravados)",
      "Pacote multipartes", "Backup de app", "Backup de dados"};
  char details[1600], fingerprint[17];
  for (unsigned i = 0; i < 8; i++)
    snprintf(fingerprint + 2 * i, 3, "%02x", s_plan.firmware.source_sha256[i]);
  int used = snprintf(
      details, sizeof(details),
      "Formato: %s\nProjeto: %s   /   versao %s\nTamanho: %s   /   capacidade "
      "%s\nDestino: %s @ 0x%08lX\n%s%s\nSHA256 da origem: %s...",
      kinds[s_plan.firmware.kind], s_plan.firmware.image.project,
      s_plan.firmware.image.version, size, capacity, s_plan.target,
      (unsigned long)s_plan.address,
      s_plan.replaces_app ? "Substitui: " : "Destino: ",
      s_plan.replaces_app ? s_plan.replaced_project
      : s_plan.slot < 0   ? "somente dados"
                          : "slot livre",
      fingerprint);
  for (unsigned i = 0; i < s_plan.firmware.data_count && used > 0 &&
                       used < (int)sizeof(details);
       i++) {
    const firmware_region_t *r = &s_plan.firmware.data[i];
    int n = snprintf(details + used, sizeof(details) - used,
                     "\nDados: %s (%lu bytes); backup integral antes de apagar",
                     r->label, (unsigned long)r->length);
    if (n < 0 || n >= (int)sizeof(details) - used)
      break;
    used += n;
  }
  lv_label_set_text(info, details);
  const char *warning =
      s_plan.needs_layout
          ? "ALOCACAO AUTOMATICA: apps existentes serao preservados.\nBackup "
            "integral antes da compactacao; reinicia e reabre a\ncopia do "
            "instalador no diretorio /launcher/incoming."
      : s_plan.firmware.data_count
          ? "Os dados listados serao substituidos apos backup "
            "verificado.\nLauncher, NVS e aurora_cfg permanecem preservados."
          : "Somente app: dados externos serao preservados.\nO .bin nao "
            "informa todos os dados/drivers/offsets exigidos.\nConfira a "
            "compatibilidade do firmware com o Tab5.";
  lv_obj_t *warning_label = text(panel, warning, 0, 274, 0xFF5967);
  lv_obj_set_width(warning_label, 790);
  s_accept = lv_checkbox_create(panel);
  lv_obj_set_pos(s_accept, 0, 370);
  lv_obj_set_width(s_accept, 790);
  lv_obj_set_style_text_color(s_accept, lv_color_hex(0xDCE2EB), 0);
  lv_checkbox_set_text(
      s_accept,
      s_plan.needs_layout ? "Aceito compactar preservando os apps e preparar "
                            "espaco para este app."
      : s_plan.slot < 0
          ? "Aceito restaurar somente os dados listados, com backup previo."
      : s_plan.firmware.data_count
          ? "Aceito gravar o app e os dados listados, com backup previo."
          : "Instalar somente o app; aceito os requisitos de dados "
            "desconhecidos.");
  lv_obj_add_event_cb(s_accept, accept_cb, LV_EVENT_VALUE_CHANGED, NULL);
  s_modal_hint =
      text(panel,
           boot_mgr_return_supported()
               ? "RESET retorna ao launcher apos um boot do app."
               : "Bootloader de retorno nao detectado. Instalacao bloqueada.",
           0, 430, 0x9CA6B5);
  lv_obj_t *cancel = button(panel, "Cancelar", 160, 48, cancel_plan);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  s_install = button(panel, "Instalar app", 220, 48, install_confirm);
  lv_obj_add_flag(s_install, LV_OBJ_FLAG_USER_2);
  lv_obj_align(s_install, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_remove_event_cb(s_modal, close_cb);
  lv_obj_add_event_cb(s_modal, cancel_plan, LV_EVENT_CANCEL, NULL);
  enable(s_install, false);
  if (!boot_mgr_return_supported()) {
    enable(s_accept, false);
  }
}
static void update_selection(void) {
  bool selected = s_selected >= 0 && s_selected < (int)s_listing.count;
  bool file = selected && !s_listing.entries[s_selected].directory;
  enable(s_rename, selected);
  enable(s_delete, selected);
  enable(s_copy, selected);
  enable(s_open, file);
  enable(s_edit, file);
  const char *ext =
      selected ? strrchr(s_listing.entries[s_selected].name, '.') : NULL;
  enable(s_analyze,
         file && ext &&
             (!strcasecmp(ext, ".bin") || !strcasecmp(ext, ".json") ||
              !strcasecmp(ext, ".raw") || !strcasecmp(ext, ".full")));
  for (unsigned i = 0; i < lv_obj_get_child_count(s_list); i++) {
    lv_obj_t *row = lv_obj_get_child(s_list, i);
    if (i == (unsigned)s_selected)
      lv_obj_add_state(row, LV_STATE_CHECKED);
    else
      lv_obj_clear_state(row, LV_STATE_CHECKED);
  }
  if (!selected) {
    lv_label_set_text(s_details,
                      "Selecione um arquivo\n\nToque na pasta para "
                      "abrir.\nSegure para selecionar.\n\n.bin: analisar e "
                      "instalar\n.txt/.conf: previa de texto\n\nCopiar: "
                      "selecione, abra a pasta\nde destino e toque em Colar.");
    return;
  }
  sd_entry_t *entry = &s_listing.entries[s_selected];
  char size[32], date[48];
  human_size(entry->size, size, sizeof(size));
  time_t stamp = entry->modified;
  struct tm tm;
  localtime_r(&stamp, &tm);
  strftime(date, sizeof(date), "%d/%m/%Y %H:%M", &tm);
  if (stamp < 946684800)
    snprintf(date, sizeof(date), "Data indisponivel");
  lv_label_set_text_fmt(s_details,
                        "%s\n\n%s\n%s\n\nOs arquivos do SD precisam ser "
                        "instalados na flash para executar.",
                        entry->name, size, date);
}
static void render_list(void) {
  lv_obj_clean(s_list);
  s_selected = -1;
  const char *root = sd_path_root(s_cwd);
  lv_label_set_text_fmt(s_path, "%c:%s",
                        root && !strcmp(root, USB_FILES_ROOT) ? 'D' : 'C',
                        root ? s_cwd + strlen(root) : "");
  for (unsigned i = 0; i < s_listing.count; i++) {
    sd_entry_t *entry = &s_listing.entries[i];
    lv_obj_t *row = button(s_list, "", 100, 66, NULL);
    lv_obj_clean(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_pad_all(row, 12, 0);
    text(row, entry->directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, 0, 9,
         0xEB2638);
    lv_obj_t *name = text(row, entry->name, 34, 0, 0xFFFFFF);
    lv_obj_set_width(name, LV_PCT(85));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    char size[32];
    if (entry->directory)
      snprintf(size, sizeof(size), "Pasta");
    else
      human_size(entry->size, size, sizeof(size));
    text(row, size, 34, 24, 0x9CA6B5);
    lv_obj_add_event_cb(row, select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(row, pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(row, long_cb, LV_EVENT_LONG_PRESSED,
                        (void *)(intptr_t)i);
  }
  if (!s_listing.count)
    text(s_list, "Pasta vazia", 12, 12, 0x9CA6B5);
  enable(s_prev, s_page_index > 0);
  enable(s_next, (s_page_index + 1) * SD_PAGE_SIZE < s_listing.total);
  lv_label_set_text_fmt(s_status, "%u itens  /  pagina %u de %u",
                        s_listing.total, s_page_index + 1,
                        s_listing.total ? (s_listing.total + SD_PAGE_SIZE - 1) /
                                              SD_PAGE_SIZE
                                        : 1);
  update_selection();
}
static void poll(lv_timer_t *timer) {
  progress_t p;
  if (xQueueReceive(s_progress, &p, 0) == pdTRUE &&
      p.generation == s_generation) {
    lv_label_set_text_fmt(s_status, "%s  /  %u%%", p.stage, p.percent);
    ui_progress_update(p.percent, p.stage);
    if (strstr(p.stage, "Gravando dados") ||
        strstr(p.stage, "Tabela verificada") ||
        strstr(p.stage, "Gravando tabela"))
      ui_progress_set_cancelable(false);
  }
  result_t *r;
  while (xQueueReceive(s_results, &r, 0) == pdTRUE) {
    if (r->generation == s_generation) {
      if (r->type == JOB_INSTALL || r->type == JOB_COPY)
        ui_progress_close();
      if (r->error)
        lv_label_set_text(s_status, r->message);
      else
        switch (r->type) {
        case JOB_LIST:
          ESP_LOGI("ui_sd", "listing ready: %u / %u entries", r->listing.count,
                   r->listing.total);
          s_listing = r->listing;
          render_list();
          break;
        case JOB_EDIT_SAVE:
          lv_label_set_text(s_status, "Texto salvo.");
          s_reload = true;
          break;
        case JOB_EDIT_LOAD:
          if (!r->truncated) {
            snprintf(s_editor_path, sizeof(s_editor_path), "%s", r->path);
            snprintf(s_editor_original, sizeof(s_editor_original), "%.4095s",
                     r->text);
            ui_form_open("EDITAR TEXTO (ate 4095 bytes)", r->text, true, 4095,
                         save_editor, NULL);
            break;
          }
          __attribute__((fallthrough));
        case JOB_PREVIEW: {
          lv_obj_t *panel =
              modal_panel(r->truncated ? "PREVIA DE TEXTO (primeiros 4 KB)"
                                       : "PREVIA DE TEXTO",
                          640);
          lv_obj_t *area = lv_obj_create(panel);
          lv_obj_set_pos(area, 0, 50);
          lv_obj_set_size(area, 800, 470);
          lv_obj_set_style_bg_color(area, lv_color_hex(0x14171C), 0);
          lv_obj_t *content = text(area, r->text, 0, 0, 0xDCE2EB);
          lv_obj_set_width(content, LV_PCT(100));
          lv_label_set_long_mode(content, LV_LABEL_LONG_WRAP);
          lv_obj_t *close = button(panel, "Fechar", 150, 48, close_cb);
          lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
          break;
        }
        case JOB_FULL_PLAN: {
          char message[512];
          const uint8_t *sha = s_full_snapshot->sha256;
          snprintf(
              message, sizeof(message),
              "Backup: %.128s\nSHA-256: "
              "%02x%02x%02x%02x%02x%02x%02x%02x...\nApps, dados e tabela "
              "voltarão ao estado desse backup.\nBootloader/launcher "
              "compatíveis. Backup atual antes de gravar.\nNão remova o SD.",
              s_full_snapshot->path, sha[0], sha[1], sha[2], sha[3], sha[4],
              sha[5], sha[6], sha[7]);
          ui_confirm("Restaurar backup integral", message, "Restaurar",
                     full_confirmed, NULL);
          break;
        }
        case JOB_ANALYZE:
          s_plan = r->plan;
          show_plan();
          lv_label_set_text(
              s_status, "Imagem analisada. Confira o plano antes de instalar.");
          break;
        case JOB_INSTALL: {
          lv_obj_t *panel = modal_panel("INSTALACAO VERIFICADA", 320);
          lv_obj_t *info = text(panel, r->message, 0, 60, 0xDCE2EB);
          lv_obj_set_width(info, 790);
          text(panel, "Iniciar executa o app uma vez. RESET volta ao launcher.",
               0, 150, 0x9CA6B5);
          lv_obj_t *stay =
              button(panel, "Continuar no launcher", 240, 48, close_cb);
          lv_obj_align(stay, LV_ALIGN_BOTTOM_LEFT, 0, 0);
          lv_obj_t *launch =
              button(panel, "Iniciar aplicativo", 220, 48, launch_cb);
          lv_obj_align(launch, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
          if (s_plan.slot < 0)
            enable(launch, false);
          lv_label_set_text(s_status, r->message);
          break;
        }
        default:
          lv_label_set_text(s_status, "Operacao concluida");
          s_reload = true;
          break;
        }
    }
    free(r);
    ui_style_refresh();
    ui_nav_refresh();
  }
  if (s_reload && !atomic_load(&s_busy)) {
    s_reload = false;
    submit(JOB_LIST, s_cwd, NULL, NULL);
  }
}
static void page_deleted(lv_event_t *e) {
  if (s_poll) {
    lv_timer_delete(s_poll);
    s_poll = NULL;
  }
  close_modal();
  ui_form_close();
  ui_progress_close();
  ++s_generation;
  s_page = s_list = s_status = s_path = s_details = NULL;
}
bool ui_sd_handle_key(const key_event_t *key) {
  if (!s_modal)
    return false;
  if (key->kind == KEY_KIND_ESC) {
    lv_obj_send_event(s_modal, LV_EVENT_CANCEL, NULL);
    return true;
  }
  lv_obj_t *focused = ui_nav_focused();
  if (key->kind == KEY_KIND_TAB || !s_field || (focused && focused != s_field))
    return false;
  if (s_field) {
    switch (key->kind) {
    case KEY_KIND_ENTER:
      dialog_confirm(NULL);
      break;
    case KEY_KIND_BACKSPACE:
      lv_textarea_delete_char(s_field);
      break;
    case KEY_KIND_DELETE:
      lv_textarea_delete_char_forward(s_field);
      break;
    case KEY_KIND_LEFT:
      lv_textarea_cursor_left(s_field);
      break;
    case KEY_KIND_RIGHT:
      lv_textarea_cursor_right(s_field);
      break;
    case KEY_KIND_CHAR:
      if (!key->ctrl && !key->alt) {
        uint32_t ch = key_char(key);
        if (ch >= 32 && ch < 127) {
          char t[2] = {(char)ch, 0};
          lv_textarea_add_text(s_field, t);
        }
      }
      break;
    default:
      break;
    }
  }
  return true;
}
lv_obj_t *ui_sd_create(lv_obj_t *parent) {
  ++s_generation;
  s_selected = -1;
  s_reload = false;
  s_page = lv_obj_create(parent);
  lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_page, lv_color_hex(0x14171C), 0);
  lv_obj_set_style_pad_all(s_page, 20, 0);
  lv_obj_set_style_border_width(s_page, 0, 0);
  lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_page, page_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_update_layout(s_page);
  int w = lv_obj_get_content_width(s_page),
      h = lv_obj_get_content_height(s_page), left = w - 350;
  lv_obj_t *heading = text(s_page, "CARTAO SD", 0, 0, 0xFFFFFF);
  lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, 0);
  lv_obj_t *up = button(s_page, LV_SYMBOL_UP " Voltar", 112, 44, up_cb);
  lv_obj_set_pos(up, 196, 36);
  s_path = text(s_page, "C:", 318, 46, 0xCCD5E1);
  lv_obj_t *drive_c = button(s_page, "C:", 90, 44, drive_cb);
  lv_obj_set_pos(drive_c, 0, 36);
  lv_obj_t *drive_d = button(s_page, "D:", 90, 44, NULL);
  lv_obj_set_pos(drive_d, 98, 36);
  lv_obj_add_event_cb(drive_d, drive_cb, LV_EVENT_CLICKED, (void *)1);
  lv_obj_set_width(s_path, left - 328);
  lv_label_set_long_mode(s_path, LV_LABEL_LONG_DOT);
  lv_obj_t *folder = button(s_page, LV_SYMBOL_PLUS " Pasta", 125, 42, mkdir_cb);
  lv_obj_set_pos(folder, left - 260, 0);
  lv_obj_t *refresh =
      button(s_page, LV_SYMBOL_REFRESH " Atualizar", 125, 42, refresh_cb);
  lv_obj_set_pos(refresh, left - 125, 0);
  s_list = lv_obj_create(s_page);
  lv_obj_set_pos(s_list, 0, 90);
  lv_obj_set_size(s_list, left, h - 148);
  lv_obj_set_style_pad_all(s_list, 0, 0);
  lv_obj_set_style_border_width(s_list, 0, 0);
  lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_row(s_list, 6, 0);
  lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
  lv_obj_t *panel = lv_obj_create(s_page);
  lv_obj_set_pos(panel, left + 16, 0);
  lv_obj_set_size(panel, 334, h - 58);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x20242B), 0);
  lv_obj_set_style_pad_all(panel, 16, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x454C58), 0);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  text(panel, "ARQUIVO SELECIONADO", 0, 0, 0xEB2638);
  s_details = text(panel, "", 0, 42, 0xDCE2EB);
  lv_obj_set_width(s_details, 296);
  lv_label_set_long_mode(s_details, LV_LABEL_LONG_WRAP);
  s_analyze = button(panel, LV_SYMBOL_DOWNLOAD " Analisar / restaurar", 296, 48,
                     analyze_cb);
  lv_obj_add_flag(s_analyze, LV_OBJ_FLAG_USER_2);
  lv_obj_align(s_analyze, LV_ALIGN_BOTTOM_MID, 0, -180);
  lv_obj_set_style_bg_color(s_analyze, lv_color_hex(0xB6172C), 0);
  s_open = button(panel, LV_SYMBOL_EYE_OPEN " Ver", 144, 42, preview_cb);
  lv_obj_align(s_open, LV_ALIGN_BOTTOM_LEFT, 0, -128);
  s_edit = button(panel, LV_SYMBOL_EDIT " Editar", 144, 42, edit_cb);
  lv_obj_align(s_edit, LV_ALIGN_BOTTOM_RIGHT, 0, -128);
  s_rename = button(panel, LV_SYMBOL_EDIT " Nome", 144, 42, rename_cb);
  lv_obj_align(s_rename, LV_ALIGN_BOTTOM_LEFT, 0, -78);
  s_copy = button(panel, LV_SYMBOL_COPY " Copiar", 144, 42, copy_cb);
  lv_obj_align(s_copy, LV_ALIGN_BOTTOM_RIGHT, 0, -78);
  s_delete = button(panel, LV_SYMBOL_TRASH " Excluir", 144, 42, delete_cb);
  lv_obj_align(s_delete, LV_ALIGN_BOTTOM_LEFT, 0, -28);
  s_paste = button(panel, LV_SYMBOL_PASTE " Colar", 144, 42, paste_cb);
  lv_obj_align(s_paste, LV_ALIGN_BOTTOM_RIGHT, 0, -28);
  enable(s_paste, s_clipboard[0]);
  s_prev = button(s_page, LV_SYMBOL_LEFT, 48, 36, prev_cb);
  lv_obj_set_pos(s_prev, 0, h - 42);
  s_next = button(s_page, LV_SYMBOL_RIGHT, 48, 36, next_cb);
  lv_obj_set_pos(s_next, 58, h - 42);
  s_status = text(s_page, "Lendo cartao...", 122, h - 30, 0x9CA6B5);
  lv_obj_set_width(s_status, w - 250);
  lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
  lv_obj_t *cancel = button(s_page, "Cancelar", 112, 36, cancel_operation);
  lv_obj_set_pos(cancel, w - 112, h - 42);
  update_selection();
  if (start_worker()) {
    s_poll = lv_timer_create(poll, 80, NULL);
    char pending[SD_PATH_MAX];
    if (s_open_source[0]) {
      submit(JOB_ANALYZE, s_open_source, NULL, NULL);
      s_open_source[0] = 0;
    } else if (installer_pending_source(pending, sizeof(pending)))
      submit(JOB_ANALYZE, pending, NULL, NULL);
    else
      submit(JOB_LIST, s_cwd, NULL, NULL);
  } else
    lv_label_set_text(s_status, "Memoria insuficiente para iniciar o SD");
  return s_page;
}

bool ui_sd_busy(void) { return atomic_load(&s_busy); }

void ui_sd_open_source(const char *path) {
  snprintf(s_open_source, sizeof(s_open_source), "%s", path);
  ui_navigate(UI_VIEW_SD);
}

void ui_sd_open_directory(const char *path) {
  if (sd_path_valid(path))
    snprintf(s_cwd, sizeof(s_cwd), "%s", path);
  s_page_index = 0;
  ui_navigate(UI_VIEW_SD);
}
