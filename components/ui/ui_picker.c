#include "ui_picker.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sd_files.h"
#include "ui.h"
#include "ui_nav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  char path[SD_PATH_MAX];
  unsigned page, generation;
} job_t;
typedef struct {
  job_t job;
  int error;
  sd_listing_t listing;
} result_t;
static QueueHandle_t jobs, results;
static lv_obj_t *modal, *list, *heading;
static lv_timer_t *timer;
static char cwd[SD_PATH_MAX];
static unsigned generation, page;
static bool busy, wall;
static result_t *current;
static ui_form_save_fn save;
static void *user;
static void worker(void *arg) {
  job_t job;
  for (;;)
    if (xQueueReceive(jobs, &job, portMAX_DELAY)) {
      result_t *r = calloc(1, sizeof(*r));
      if (!r) {
        r = NULL;
        xQueueSend(results, &r, portMAX_DELAY);
        continue;
      }
      r->job = job;
      r->error =
          sd_files_list_filtered(job.path, job.page, &r->listing, NULL, true);
      xQueueSend(results, &r, portMAX_DELAY);
    }
}
static void gone(lv_event_t *e) {
  modal = list = heading = NULL;
  ++generation;
  free(current);
  current = NULL;
  save = NULL;
  user = NULL;
}
void ui_picker_close(void) {
  if (modal)
    lv_obj_delete(modal);
}
static void close_cb(lv_event_t *e) { ui_picker_close(); }
static void request(void) {
  if (busy || !modal)
    return;
  job_t job = {.page = page, .generation = generation};
  snprintf(job.path, sizeof(job.path), "%s", cwd);
  if (xQueueSend(jobs, &job, 0)) {
    busy = true;
    lv_label_set_text(heading, "Lendo pastas e imagens...");
  }
}
static void selected(lv_event_t *e) {
  if (busy || !current)
    return;
  unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
  if (i >= current->listing.count)
    return;
  sd_entry_t *entry = &current->listing.entries[i];
  char path[SD_PATH_MAX];
  if (!sd_path_join(path, sizeof(path), cwd, entry->name))
    return;
  if (entry->directory) {
    snprintf(cwd, sizeof(cwd), "%s", path);
    page = 0;
    request();
  } else if (save && save(path, user))
    ui_picker_close();
  else if (heading)
    lv_label_set_text(
        heading, "Não foi possível salvar. Verifique o tamanho do caminho.");
}
static lv_obj_t *button(lv_obj_t *p, const char *t, lv_event_cb_t cb, void *u) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_size(b, LV_PCT(100), 44);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, u);
  return b;
}
static void poll(lv_timer_t *t) {
  result_t *r;
  while (xQueueReceive(results, &r, 0)) {
    busy = false;
    if (!r) {
      if (heading)
        lv_label_set_text(heading, "Memória insuficiente; abra novamente.");
      continue;
    }
    if (!modal || r->job.generation != generation) {
      free(r);
      if (modal)
        request();
      continue;
    }
    free(current);
    current = r;
    lv_obj_clean(list);
    lv_label_set_text_fmt(heading, "C:%s / página %u / %u itens",
                          cwd + strlen(SD_FILES_ROOT), page + 1,
                          r->listing.total);
    if (r->error)
      lv_label_set_text(heading, "Não foi possível ler a pasta.");
    for (unsigned i = 0; i < r->listing.count; i++) {
      char text[SD_NAME_MAX + 8];
      snprintf(text, sizeof(text), "%s %s",
               r->listing.entries[i].directory ? LV_SYMBOL_DIRECTORY
                                               : LV_SYMBOL_IMAGE,
               r->listing.entries[i].name);
      button(list, text, selected, (void *)(uintptr_t)i);
    }
    ui_nav_refresh();
    ui_style_refresh();
  }
}
static void up(lv_event_t *e) {
  if (busy)
    return;
  char *p = strrchr(cwd, '/');
  if (p && p >= cwd + strlen(SD_FILES_ROOT))
    *p = 0;
  page = 0;
  request();
}
static void pages(lv_event_t *e) {
  if (busy || !current)
    return;
  int d = (int)(intptr_t)lv_event_get_user_data(e);
  if (d < 0 && page)
    page--;
  else if (d > 0 && (page + 1) * SD_PAGE_SIZE < current->listing.total)
    page++;
  else
    return;
  request();
}
static void clear(lv_event_t *e) {
  if (save && save(wall ? "-" : "", user))
    ui_picker_close();
}
void ui_picker_open(const char *title, bool wallpaper, ui_form_save_fn fn,
                    void *arg) {
  ui_picker_close();
  if (!jobs) {
    jobs = xQueueCreate(1, sizeof(job_t));
    results = xQueueCreate(2, sizeof(result_t *));
    if (!jobs || !results ||
        xTaskCreate(worker, "image_picker", 8192, NULL, 2, NULL) != pdPASS) {
      if (jobs)
        vQueueDelete(jobs);
      if (results)
        vQueueDelete(results);
      jobs = results = NULL;
      ui_notice("Imagens", "Memória insuficiente");
      return;
    }
    timer = lv_timer_create(poll, 100, NULL);
    (void)timer;
  }
  save = fn;
  user = arg;
  wall = wallpaper;
  page = 0;
  snprintf(cwd, sizeof(cwd), "%s", SD_FILES_ROOT);
  ++generation;
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_text_font(modal, &lv_font_montserrat_20, 0);
  lv_obj_add_event_cb(modal, gone, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(modal, close_cb, LV_EVENT_CANCEL, NULL);
  ui_nav_modal(modal);
  lv_obj_t *p = lv_obj_create(modal);
  lv_obj_set_size(p, 1040, 630);
  lv_obj_center(p);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, title);
  heading = lv_label_create(p);
  list = lv_obj_create(p);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_t *tools = lv_obj_create(p);
  lv_obj_set_size(tools, LV_PCT(100), 64);
  lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
  lv_obj_t *b = button(tools, LV_SYMBOL_UP " Pasta", up, NULL);
  lv_obj_set_width(b, 160);
  b = button(tools, LV_SYMBOL_LEFT, pages, (void *)-1);
  lv_obj_set_width(b, 90);
  b = button(tools, LV_SYMBOL_RIGHT, pages, (void *)1);
  lv_obj_set_width(b, 90);
  b = button(tools, "Remover imagem", clear, NULL);
  lv_obj_set_width(b, 230);
  b = button(tools, "Cancelar", close_cb, NULL);
  lv_obj_set_width(b, 200);
  ui_nav_refresh();
  ui_nav_focus(b);
  request();
  ui_style_refresh();
}
