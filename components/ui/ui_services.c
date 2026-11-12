#include "flash_snapshot.h"
#include "launcher_store.h"
/* Worker-owned catalog model; only LVGL timer reads completed results. */
#include "backup.h"
#include "catalog.h"
#include "catalog_tracking.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "partitions_mgr.h"
#include "pi_link.h"
#include "ui.h"
#include "ui_assets.h"
#include "ui_form.h"
#include "ui_nav.h"
#include "web.h"
#include "wifi_mgr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_obj_t *s_page, *s_status, *s_items, *s_token_label;
static lv_timer_t *s_timer;
static QueueHandle_t s_jobs, s_results;
static catalog_list_t *s_catalog;
static char (*s_origins)[512];
static lv_obj_t *s_detail_panel;
static catalog_updates_t *s_updates;
static bool s_show_updates;
extern void ui_sd_open_directory(const char *);
static catalog_item_t *selected_item(unsigned i) {
  return s_show_updates
             ? (s_updates && i < s_updates->count ? &s_updates->rows[i].item
                                                  : NULL)
             : (s_catalog && i < s_catalog->count ? &s_catalog->items[i]
                                                  : NULL);
}
static const char *selected_source(unsigned i, char source[512]) {
  if (s_show_updates && s_updates && i < s_updates->count)
    return s_updates->rows[i].source;
  if (s_origins && s_catalog && i < s_catalog->count) {
    snprintf(source, 512, "%s", s_origins[i]);
    return source;
  }
  catalog_source_get(source, 512);
  return source;
}
static unsigned s_catalog_page, s_detail_index;
static bool s_detail_pending;
static lv_obj_t *s_wait_panel;
extern void ui_sd_open_source(const char *path);
static void details_render(void);
static void link_open(lv_event_t *);
static uint8_t s_layout_sha[32];
typedef struct {
  uint32_t sizes[16], minimum[16], factory, capacity;
  lv_obj_t *bar, *summary, *rows, *sliders[16], *values[16], *apply;
  bool ready, dirty;
} layout_ui_t;
static layout_ui_t *s_layout;
static void layout_populate(const flash_allocation_t *);
static void layout_render(void);
static void manual_confirmed(void *);

static void layout_confirmed(void *);

static _Atomic bool s_busy, s_cancel;
static _Atomic unsigned s_percent;
typedef struct {
  int kind;
  unsigned index;
  char url[512];
  uint32_t sizes[16];
  unsigned slots;
  uint8_t table_sha[32];
} job_t;
typedef struct {
  int kind;
  esp_err_t error;
  char message[600];
  flash_allocation_t layout;
} result_t;
static bool progress(unsigned n, const char *stage, void *user) {
  atomic_store(&s_percent, n);
  return !atomic_load(&s_cancel);
}
static void worker(void *arg) {
  job_t job;
  for (;;)
    if (xQueueReceive(s_jobs, &job, portMAX_DELAY) == pdTRUE) {
      result_t r = {.kind = job.kind};
      if (job.kind == 0 || job.kind == 8) {
        s_show_updates = false;
        r.error =
            catalog_fetch_sources(s_catalog, s_origins, job.kind == 8,
                                  r.message, sizeof(r.message), progress, NULL);
      } else if ((job.kind == 1 || job.kind == 10) &&
                 selected_item(job.index)) {
        char source[512];
        r.error = catalog_download_from(
            selected_item(job.index), selected_source(job.index, source),
            r.message, sizeof(r.message), progress, NULL);
        if (r.error)
          snprintf(r.message, sizeof(r.message),
                   "Download cancelado ou falhou: %s",
                   esp_err_to_name(r.error));
      } else if (job.kind == 9 && selected_item(job.index)) {
        r.error = catalog_probe(selected_item(job.index), progress, NULL);
      } else if (job.kind == 11) {
        flash_allocation_t allocation;
        r.error =
            flash_allocate_plan(0, &allocation, r.message, sizeof(r.message));
        if (!r.error)
          memcpy(s_layout_sha, allocation.digest, 32);
      } else if (job.kind == 12) {
        r.error = installer_compact(job.table_sha, progress, NULL, r.message,
                                    sizeof(r.message));
        if (!r.error) {
          vTaskDelay(pdMS_TO_TICKS(400));
          esp_restart();
        }
      } else if (job.kind == 21) {
        r.error =
            flash_allocate_plan(0, &r.layout, r.message, sizeof(r.message));
        if (!r.error)
          snprintf(r.message, sizeof(r.message),
                   "Mapa atual: ajuste as reservas ou use Auto para compactar. "
                   "Apps e dados sao preservados.");
      } else if (job.kind == 22) {
        r.error = flash_layout_plan(job.sizes, &r.layout, r.message,
                                    sizeof(r.message));
        if (!r.error)
          memcpy(s_layout_sha, r.layout.digest, 32);
      } else if (job.kind == 23) {
        r.error = installer_layout_apply(job.sizes, job.table_sha, progress,
                                         NULL, r.message, sizeof(r.message));
        if (!r.error) {
          vTaskDelay(pdMS_TO_TICKS(400));
          esp_restart();
        }
      } else if (job.kind == 13) {
        flash_snapshot_t snapshot;
        r.error = installer_full_backup(&snapshot, progress, NULL, r.message,
                                        sizeof(r.message));
      } else if (job.kind == 16) {
        r.error = installer_resume_flash(progress, NULL, r.message,
                                         sizeof(r.message));
        if (!r.error) {
          vTaskDelay(pdMS_TO_TICKS(300));
          esp_restart();
        }
      } else if (job.kind == 17) {
        r.error = catalog_check_updates(s_updates, progress, NULL);
        if (!r.error)
          s_show_updates = true;
        unsigned changes = 0;
        for (unsigned i = 0; i < s_updates->count; i++)
          if (s_updates->rows[i].changed)
            changes++;
        snprintf(r.message, sizeof(r.message),
                 "%u versão(ões) diferente(s); %u app(s) sem origem OTA; %u "
                 "fonte(s) falharam.",
                 changes, s_updates->untracked, s_updates->failed);
      } else if (job.kind == 18) {
        r.error = catalog_download_updates(s_updates, progress, NULL, r.message,
                                           sizeof(r.message));
      } else if (job.kind == 20) {
        r.error = installer_recover_profile(true, progress, NULL, r.message,
                                            sizeof(r.message));
      } else if (job.kind == 19) {
        char source[512];
        r.error = catalog_link_slot(job.slots, selected_item(job.index),
                                    selected_source(job.index, source));
        snprintf(r.message, sizeof(r.message),
                 "Origem associada ao slot %u: %s", job.slots,
                 esp_err_to_name(r.error));
      } else if (job.kind == 2) {
        r.error = web_start();
        snprintf(r.message, sizeof(r.message), "Servidor: %s",
                 esp_err_to_name(r.error));
      } else if (job.kind == 4 || job.kind == 5 || job.kind == 6) {
        r.error = job.kind == 4   ? pi_link_init()
                  : job.kind == 5 ? pi_link_sync_time()
                                  : pi_link_shutdown();
        snprintf(r.message, sizeof(r.message), "Pi: %s",
                 esp_err_to_name(r.error));
      } else if (job.kind == 7) {
        r.error = backup_slot(job.index, progress, NULL, r.message,
                              sizeof(r.message));
      } else if (job.kind == 3) {
        web_stop();
        snprintf(r.message, sizeof(r.message),
                 "Servidor encerrado; token invalidado");
      }
      xQueueSend(s_results, &r, portMAX_DELAY);
    }
}
static bool submit(int kind, unsigned index, const char *url) {
  if (!s_jobs || atomic_exchange(&s_busy, true))
    return false;
  job_t job = {.kind = kind, .index = index};
  if (kind == 19)
    job.slots = index >> 16;
  if (kind == 19)
    job.index = index & 65535;
  if (kind == 12 || kind == 23)
    memcpy(job.table_sha, s_layout_sha, 32);
  if ((kind == 22 || kind == 23) && s_layout)
    memcpy(job.sizes, s_layout->sizes, sizeof(job.sizes));

  if (url)
    snprintf(job.url, sizeof(job.url), "%s", url);
  atomic_store(&s_cancel, false);
  atomic_store(&s_percent, 0);
  if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
    atomic_store(&s_busy, false);
    return false;
  }
  return true;
}
static lv_obj_t *button(lv_obj_t *parent, const char *name, lv_event_cb_t cb,
                        void *user) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_set_width(b, LV_PCT(100));
  lv_obj_set_height(b, 48);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, name);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
  return b;
}
static void download(lv_event_t *e) {
  unsigned kind = (unsigned)(uintptr_t)lv_event_get_user_data(e);
  if (submit(kind, s_detail_index, NULL)) {
    ui_form_close();
    s_detail_pending = false;
  }
}
static void details_render(void) {
  if (!s_page || !selected_item(s_detail_index))
    return;
  const catalog_item_t *item = selected_item(s_detail_index);
  uint64_t total = 0;
  bool known = true;
  for (unsigned i = 0; i < item->count; i++) {
    total += item->parts[i].size;
    if (!item->parts[i].size)
      known = false;
  }
  char text[1400], size[80];
  if (known)
    snprintf(size, sizeof(size), "%.2f MiB (%llu bytes)",
             (double)total / 1048576, (unsigned long long)total);
  else
    snprintf(size, sizeof(size), "Não informado pela origem");
  snprintf(text, sizeof(text), "Versão: %s\nTamanho: %s\nSHA-256: %s\n\n%s",
           item->version, size,
           item->parts[0].sha256[0] ? "publicado pela origem"
                                    : "origem sem SHA; integridade local",
           item->description[0] ? item->description
                                : "Descrição não disponível na origem.");
  if (!s_detail_panel)
    return;
  lv_obj_clean(s_detail_panel);
  lv_obj_t *title = lv_label_create(s_detail_panel);
  lv_label_set_text(title, item->name);
  lv_obj_set_width(title, LV_PCT(100));
  lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
  if (item->cover_path[0]) {
    lv_obj_t *image = lv_image_create(s_detail_panel);
    ui_assets_set(image, item->cover_path, 1280, 1280, 270, 130);
  }
  lv_obj_t *description = lv_label_create(s_detail_panel);
  lv_obj_set_width(description, LV_PCT(100));
  lv_label_set_long_mode(description, LV_LABEL_LONG_WRAP);
  lv_label_set_text(description, text);
  button(s_detail_panel, "Baixar e instalar", download, (void *)10);
  button(s_detail_panel, "Apenas baixar", download, (void *)1);
  button(s_detail_panel, "Vincular app instalado", link_open, NULL);
  ui_nav_refresh();
  ui_style_refresh();
}
static void detail(lv_event_t *e) {
  unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
  if (ui_services_busy() || !selected_item(i))
    return;
  s_detail_index = i;
  if (s_detail_panel) {
    lv_obj_clean(s_detail_panel);
    lv_obj_t *l = lv_label_create(s_detail_panel);
    lv_label_set_text(l, "Consultando tamanho e imagem...");
  }
  s_detail_pending = submit(9, i, NULL);
  s_wait_panel = s_detail_panel;
}
static void show_items(void) {
  if (!s_items || atomic_load(&s_busy))
    return;
  lv_obj_clean(s_items);
  unsigned count = s_show_updates ? (s_updates ? s_updates->count : 0)
                                  : (s_catalog ? s_catalog->count : 0);
  for (unsigned i = s_catalog_page * 24;
       i < count && i < (s_catalog_page + 1) * 24; i++) {
    const catalog_item_t *item = selected_item(i);
    char name[192];
    if (s_show_updates)
      snprintf(name, sizeof(name), "%s: %s " LV_SYMBOL_RIGHT " %s%s",
               item->name, s_updates->rows[i].previous, item->version,
               s_updates->rows[i].changed ? " / diferente" : " / igual");
    else
      snprintf(name, sizeof(name), "%s / %s / %u arquivo(s)", item->name,
               item->version, item->count);
    button(s_items, name, detail, (void *)(uintptr_t)i);
  }
  ui_nav_refresh();
  ui_style_refresh();
}
static void change_page(lv_event_t *e) {
  if (ui_services_busy() || !s_catalog)
    return;
  int direction = (int)(intptr_t)lv_event_get_user_data(e);
  if (direction < 0 && s_catalog_page)
    s_catalog_page--;
  else if (direction > 0 &&
           (s_catalog_page + 1) * 24 <
               (s_show_updates ? s_updates->count : s_catalog->count))
    s_catalog_page++;
  show_items();
}
static void refresh(lv_event_t *e) { submit(8, 0, NULL); }
static void cancel(lv_event_t *e) { atomic_store(&s_cancel, true); }
static void start_web(lv_event_t *e) { submit(web_running() ? 3 : 2, 0, NULL); }
static void poll(lv_timer_t *t) {
  if (s_layout && s_layout->ready && s_layout->dirty)
    layout_render();
  result_t r;
  while (s_results && xQueueReceive(s_results, &r, 0) == pdTRUE) {
    atomic_store(&s_busy, false);
    if (s_status) {
      if ((r.kind == 1 || r.kind == 10) && !r.error)
        lv_label_set_text_fmt(
            s_status, "Salvo: %s\nAbra a pasta em SD para analisar e instalar.",
            r.message);
      else
        lv_label_set_text(s_status, r.message);
    }
    if (r.kind == 11 && !r.error && s_page &&
        lv_obj_get_user_data(s_page) == (void *)3) {
      ui_confirm(
          "Reparticionar flash",
          "Os apps serão copiados do backup para os novos slots.\nUm layout "
          "sem espaço para todos é recusado.\nBackup integral e verificação "
          "SHA-256 antes da alteração.\nOperações interrompidas podem ser "
          "retomadas em FLASH.\nNão remova o SD. O Tab5 reinicia ao concluir.",
          "Aplicar e reiniciar", layout_confirmed, NULL);
    }
    if (r.kind == 21 && !r.error && s_layout)
      layout_populate(&r.layout);
    if (r.kind == 22 && !r.error && s_layout)
      ui_confirm("Aplicar mapa da flash", r.message, "Aplicar e reiniciar",
                 manual_confirmed, NULL);
    if (r.kind == 17 && !r.error) {
      s_catalog_page = 0;
      show_items();
    }
    if (r.kind == 9 && s_detail_pending && s_detail_panel == s_wait_panel) {
      s_detail_pending = false;
      details_render();
    }
    if (r.kind == 10 && !r.error && s_page) {
      ui_sd_open_source(r.message);
      return;
    }
    if (r.kind == 0 || r.kind == 8) {
      s_catalog_page = 0;
      show_items();
    }
  }
  if (atomic_load(&s_busy) && s_status)
    lv_label_set_text_fmt(s_status, "Operacao em andamento: %u%%",
                          atomic_load(&s_percent));
  if (s_page && lv_obj_get_user_data(s_page) == (void *)1 && web_running()) {
    char token[16];
    web_access_token(token, sizeof(token));
    wifi_status_t w;
    wifi_get_status(&w);
    lv_label_set_text_fmt(
        s_status,
        "http://%s/\nCodigo: %s\nHTTP: use em rede de confianca. "
        "Token expira em 15 minutos; encerrar invalida "
        "imediatamente.\nInstalacoes continuam exigindo "
        "confirmacao no Tab5.",
        w.ip, token);
    if (s_token_label)
      lv_label_set_text(s_token_label, token);
  }
  if (s_page && lv_obj_get_user_data(s_page) == (void *)2 &&
      !atomic_load(&s_busy)) {
    pi_link_status_t p;
    pi_link_status(&p);
    lv_label_set_text_fmt(
        s_status,
        "Host %s / USB %s / Pi %s\n%s %s / IP %s\n%s\nShutdown envia comando; "
        "nao prova que o Pi terminou de gravar.",
        p.started ? "ativo" : "inativo", p.usb ? "conectado" : "ausente",
        p.online ? "online" : "sem HELLO", p.host, p.version, p.ip, p.error);
  }
}
static void deleted(lv_event_t *e) {
  s_page = s_status = s_items = s_token_label = s_detail_panel = NULL;
  s_detail_pending = false;
  free(s_layout);
  s_layout = NULL;
  ui_form_close();
}
static void pi_action(lv_event_t *e) {
  submit((int)(intptr_t)lv_event_get_user_data(e), 0, NULL);
}
static lv_obj_t *toolbar(lv_obj_t *p) {
  lv_obj_t *r = lv_obj_create(p);
  lv_obj_set_size(r, LV_PCT(100), 52);
  lv_obj_set_style_pad_all(r, 0, 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_pad_column(r, 10, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  return r;
}
static void tool_button(lv_obj_t *r, const char *text, lv_event_cb_t callback,
                        void *user) {
  lv_obj_t *b = button(r, text, callback, user);
  lv_obj_set_width(b, 0);
  lv_obj_set_flex_grow(b, 1);
}
static void layout_plan_cb(lv_event_t *e) { submit(11, 0, NULL); }
static void layout_confirmed(void *user) { submit(12, 0, NULL); }
static void layout_segment(const char *name, uint32_t size, uint32_t color) {
  if (!size)
    return;
  lv_obj_t *o = lv_obj_create(s_layout->bar);
  lv_obj_add_flag(o, LV_OBJ_FLAG_USER_1);
  lv_obj_set_size(o, 0, LV_PCT(100));
  lv_obj_set_flex_grow(o, (size + 65535) / 65536);
  lv_obj_set_style_pad_all(o, 1, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 3, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  if (size >= 0x100000) {
    lv_obj_t *l = lv_label_create(o);
    lv_label_set_text(l, name);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    unsigned red = (color >> 16) & 255, green = (color >> 8) & 255,
             blue = color & 255;
    uint32_t light = red * red * 299 + green * green * 587 + blue * blue * 114;
    lv_obj_set_style_text_color(
        l, lv_color_hex(light > 12000000 ? 0x101010 : 0xffffff), 0);
    lv_obj_center(l);
  }
}
static void layout_render(void) {
  if (!s_layout || !s_layout->ready)
    return;
  s_layout->dirty = false;
  uint64_t used = 0;
  for (unsigned i = 0; i < 16; i++)
    used += s_layout->sizes[i];
  uint32_t free = used <= s_layout->capacity ? s_layout->capacity - used : 0;
  char summary[180];
  snprintf(summary, sizeof(summary),
           "Launcher %.2f MiB  /  Reservas %.2f MiB  /  Livre %.2f MiB%s",
           s_layout->factory / 1048576., used / 1048576., free / 1048576.,
           used > s_layout->capacity ? "  - EXCEDE A FLASH" : "");
  lv_label_set_text(s_layout->summary, summary);
  lv_obj_set_style_text_color(
      s_layout->summary,
      lv_color_hex(used > s_layout->capacity ? 0xEF5368 : 0xDCE2EB), 0);
  if (s_layout->apply) {
    if (used > s_layout->capacity)
      lv_obj_add_state(s_layout->apply, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(s_layout->apply, LV_STATE_DISABLED);
  }
  lv_obj_clean(s_layout->bar);
  layout_segment("Boot", 0x20000, 0x454C58);
  layout_segment("Launcher", s_layout->factory, 0xEB5368);
  static const uint32_t palette[] = {0x3984C6, 0x9B6FD4, 0x2A9D8F,
                                     0xD39B43, 0x506DCC, 0xB45E8B};
  for (unsigned i = 0; i < 16; i++)
    if (s_layout->sizes[i]) {
      char label[16];
      snprintf(label, sizeof(label), "App %u", i);
      layout_segment(label, s_layout->sizes[i], palette[i % 6]);
    }
  layout_segment("Livre", free, 0x355B49);
  layout_segment("Dados", 0x5f0000, 0x626976);
}
static void layout_changed(lv_event_t *e) {
  unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
  if (!s_layout || !s_layout->ready || i >= 16)
    return;
  s_layout->sizes[i] =
      (uint32_t)lv_slider_get_value(lv_event_get_target(e)) * 65536;
  char value[32];
  snprintf(value, sizeof(value), "%.2f MiB", s_layout->sizes[i] / 1048576.);
  lv_label_set_text(s_layout->values[i], value);
  s_layout->dirty = true;
}
static void layout_populate(const flash_allocation_t *a) {
  if (!s_layout || !a->launcher_size)
    return;
  s_layout->factory = a->launcher_size;
  s_layout->capacity = 0xa10000 - 0x20000 - a->launcher_size;
  memcpy(s_layout->sizes, a->reserved, sizeof(s_layout->sizes));
  memcpy(s_layout->minimum, a->minimum, sizeof(s_layout->minimum));
  for (unsigned i = 0; i < 16; i++) {
    lv_slider_set_range(s_layout->sliders[i], s_layout->minimum[i] / 65536,
                        s_layout->capacity / 65536);
    lv_slider_set_value(s_layout->sliders[i], s_layout->sizes[i] / 65536,
                        LV_ANIM_OFF);
    char value[32];
    snprintf(value, sizeof(value), "%.2f MiB", s_layout->sizes[i] / 1048576.);
    lv_label_set_text(s_layout->values[i], value);
    lv_obj_clear_state(s_layout->sliders[i], LV_STATE_DISABLED);
  }
  s_layout->ready = true;
  layout_render();
  lv_obj_update_layout(s_layout->rows);
  lv_obj_invalidate(s_layout->rows);
}
static void manual_plan(lv_event_t *e) {
  if (s_layout && s_layout->ready)
    submit(22, 0, NULL);
}
static void manual_confirmed(void *u) {
  if (s_layout && s_layout->ready)
    submit(23, 0, NULL);
}
static void layout_editor(lv_obj_t *p) {
  s_layout = calloc(1, sizeof(*s_layout));
  if (!s_layout) {
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, "Sem memoria para o mapa.");
    return;
  }
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_width(l, LV_PCT(100));
  lv_label_set_text(
      l,
      "Arraste as reservas em passos de 64 KiB. Apps existentes e dados sao "
      "preservados. A instalacao automatica pode reorganizar essas reservas.");
  s_layout->bar = lv_obj_create(p);
  lv_obj_set_size(s_layout->bar, LV_PCT(100), 54);
  lv_obj_set_style_pad_all(s_layout->bar, 0, 0);
  lv_obj_set_style_pad_column(s_layout->bar, 2, 0);
  lv_obj_set_flex_flow(s_layout->bar, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(s_layout->bar, LV_OBJ_FLAG_SCROLLABLE);
  s_layout->summary = lv_label_create(p);
  lv_label_set_text(s_layout->summary, "Lendo mapa da flash...");
  lv_obj_set_width(s_layout->summary, LV_PCT(100));
  lv_obj_t *rows = lv_obj_create(p);
  s_layout->rows = rows;
  lv_obj_set_size(rows, LV_PCT(100), 0);
  lv_obj_set_flex_grow(rows, 1);
  lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(rows, 4, 0);
  for (unsigned i = 0; i < 16; i++) {
    lv_obj_t *r = lv_obj_create(rows);
    lv_obj_set_size(r, LV_PCT(100), 54);
    lv_obj_set_style_pad_all(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *name = lv_label_create(r);
    lv_obj_set_width(name, 90);
    lv_label_set_text_fmt(name, "Area %u", i);
    lv_obj_t *slider = lv_slider_create(r);
    s_layout->sliders[i] = slider;
    lv_obj_set_width(slider, 0);
    lv_obj_set_style_pad_all(slider, 12, LV_PART_KNOB);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_add_state(slider, LV_STATE_DISABLED);
    lv_obj_add_event_cb(slider, layout_changed, LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)i);
    s_layout->values[i] = lv_label_create(r);
    lv_obj_set_width(s_layout->values[i], 112);
    lv_obj_set_style_text_align(s_layout->values[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_layout->values[i], "-- MiB");
  }
  l = lv_label_create(p);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
  lv_label_set_text(l, "Configuracoes: C:/launcher/config.json. aurora_cfg e "
                       "uma particao legada de compatibilidade, nao usada pelo "
                       "launcher. Backup integral antes de alterar o mapa.");
  if (!ui_services_busy())
    submit(21, 0, NULL);
}
static lv_obj_t *flash_tool(lv_obj_t *p, const char *caption, lv_event_cb_t cb,
                            void *u) {
  lv_obj_t *b = button(p, caption, cb, u);
  lv_obj_set_height(b, 44);
  lv_obj_t *l = lv_obj_get_child(b, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_18, 0);
  return b;
}
static void restore_open(lv_event_t *e) {
  ui_sd_open_directory(LAUNCHER_BACKUPS);
}
static void rollback_confirmed(void *u) { submit(20, 0, NULL); }
static void service_action(lv_event_t *e) {
  if ((int)(intptr_t)lv_event_get_user_data(e) == 20) {
    ui_confirm("Reverter troca de dados",
               "Os dados anteriores à troca interrompida serão restaurados do "
               "snapshot de segurança.",
               "Reverter", rollback_confirmed, NULL);
    return;
  }
  submit((int)(intptr_t)lv_event_get_user_data(e), 0, NULL);
}
static void link_slot(lv_event_t *e) {
  submit(19,
         ((unsigned)(uintptr_t)lv_event_get_user_data(e) << 16) |
             s_detail_index,
         NULL);
  ui_form_close();
}
static void link_open(lv_event_t *e) {
  ui_notice("Associar origem OTA",
            "Escolha o aplicativo instalado que corresponde a esta entrada. A "
            "versão atual será lida do descritor.");
  lv_obj_t *p = ui_form_panel();
  lv_obj_set_height(p, 600);
  for (unsigned i = 0; i < 16; i++) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + i, NULL);
    if (!part)
      continue;
    char text[64];
    snprintf(text, sizeof(text), "Associar slot %u (%s)", i, part->label);
    lv_obj_t *b = button(p, text, link_slot, (void *)(uintptr_t)i);
    lv_obj_set_pos(b, 0, 140 + i * 48);
  }
}
bool ui_services_prepare_worker(void) {
  if (!s_jobs) {
    s_catalog = calloc(1, sizeof(*s_catalog));
    s_origins = calloc(CATALOG_MAX_ITEMS, sizeof(*s_origins));
    s_updates = calloc(1, sizeof(*s_updates));
    s_jobs = xQueueCreate(1, sizeof(job_t));
    s_results = xQueueCreate(2, sizeof(result_t));
    if (!s_catalog || !s_origins || !s_updates || !s_jobs || !s_results ||
        xTaskCreate(worker, "services", 16384, NULL, 3, NULL) != pdPASS) {
      if (s_jobs)
        vQueueDelete(s_jobs);
      if (s_results)
        vQueueDelete(s_results);
      free(s_catalog);
      free(s_origins);
      s_origins = NULL;
      free(s_updates);
      s_updates = NULL;
      s_catalog = NULL;
      s_jobs = s_results = NULL;
      return false;
    }
  }
  return true;
}
static lv_obj_t *create(lv_obj_t *parent, unsigned mode) {
  if (!ui_services_prepare_worker()) {
    ui_notice("Servicos", "Memoria insuficiente");
    return NULL;
  }
  bool web = mode == 1;
  s_page = lv_obj_create(parent);
  lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
  lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_user_data(s_page, (void *)(uintptr_t)mode);
  lv_obj_add_event_cb(s_page, deleted, LV_EVENT_DELETE, NULL);
  lv_obj_t *title = lv_label_create(s_page);
  lv_label_set_text(title, web ? "WebUI / transferencia pelo navegador"
                               : "Catalogo OTA / pacotes para ESP32-P4");
  s_status = lv_label_create(s_page);
  lv_obj_set_width(s_status, LV_PCT(100));
  lv_label_set_text(s_status,
                    web ? "Conecte o Wi-Fi e inicie o servidor. Acesso "
                          "protegido por token temporario."
                        : "M5Burner/Tab5, manifesto de atualizacao ou catalogo "
                          "schema 1.\nSem SHA na origem: integridade externa "
                          "nao verificada. Confirme a instalacao no SD.");
  if (mode == 2) {
    lv_label_set_text(title, "Raspberry Pi / USB-A / Tab5Link");
    lv_label_set_text(s_status,
                      "Integracao opcional. Sem Pi, o launcher continua "
                      "independente.\nProtocolo compilado; validacao fisica "
                      "pendente. ASK remoto nao habilitado.");
    button(s_page, "Iniciar host USB e detectar Pi", pi_action, (void *)4);
    button(s_page, "Sincronizar horario do Pi", pi_action, (void *)5);
    button(s_page, "Solicitar shutdown (nao corta 5 V)", pi_action, (void *)6);
  } else if (mode == 3) {
    lv_label_set_text(title, "Particoes / backup verificado");
    lv_label_set_text(s_status,
                      "Backup manual integral dos slots OTA com SHA-256 e "
                      "manifesto.\nRestauracao: selecione .raw no SD. "
                      "Editor altera apenas o banco de aplicativos.");
    lv_obj_t *body = lv_obj_create(s_page);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *editor = lv_obj_create(body);
    lv_obj_set_size(editor, 0, LV_PCT(100));
    lv_obj_set_flex_grow(editor, 1);
    lv_obj_set_flex_flow(editor, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(editor, 8, 0);
    lv_obj_clear_flag(editor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *actions = lv_obj_create(body);
    lv_obj_set_size(actions, 128, LV_PCT(100));
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(actions, 5, 0);
    layout_editor(editor);
    lv_obj_t *apply =
        flash_tool(actions, LV_SYMBOL_OK " Aplicar", manual_plan, NULL);
    lv_obj_add_state(apply, LV_STATE_DISABLED);
    if (s_layout)
      s_layout->apply = apply;
    flash_tool(actions, LV_SYMBOL_REFRESH " Auto", layout_plan_cb, NULL);
    flash_tool(actions, LV_SYMBOL_SAVE " Bkp.", service_action, (void *)13);
    flash_tool(actions, LV_SYMBOL_UPLOAD " Rest.", restore_open, NULL);
    if (flash_operation_pending())
      flash_tool(actions, LV_SYMBOL_REFRESH " Retomar", service_action,
                 (void *)16);
    if (app_data_recovery_required())
      flash_tool(actions, LV_SYMBOL_REFRESH " Dados", service_action,
                 (void *)20);
  } else if (web) {
    s_token_label = lv_label_create(s_page);
    lv_label_set_text(s_token_label, "--------");
    lv_obj_set_style_text_font(s_token_label, &lv_font_montserrat_32, 0);
    button(s_page, "Iniciar / encerrar servidor", start_web, NULL);
  } else {
    lv_obj_t *updates = toolbar(s_page);
    tool_button(updates, "Consultar atualizações dos apps", service_action,
                (void *)17);
    tool_button(updates, "Baixar versões diferentes em lote", service_action,
                (void *)18);
    lv_obj_t *actions = toolbar(s_page);

    tool_button(actions, "Atualizar catalogos", refresh, NULL);
    tool_button(actions, "Cancelar download", cancel, NULL);
    lv_obj_t *browser = lv_obj_create(s_page);
    lv_obj_set_width(browser, LV_PCT(100));
    lv_obj_set_flex_grow(browser, 1);
    lv_obj_set_flex_flow(browser, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(browser, LV_OBJ_FLAG_SCROLLABLE);
    s_items = lv_obj_create(browser);
    lv_obj_set_width(s_items, 0);
    lv_obj_set_flex_grow(s_items, 1);
    lv_obj_set_height(s_items, LV_PCT(100));
    lv_obj_set_flex_flow(s_items, LV_FLEX_FLOW_COLUMN);
    s_detail_panel = lv_obj_create(browser);
    lv_obj_set_width(s_detail_panel, 340);
    lv_obj_set_height(s_detail_panel, LV_PCT(100));
    lv_obj_set_flex_flow(s_detail_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *hint = lv_label_create(s_detail_panel);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_label_set_text(
        hint, "Selecione um app na lista.\nFontes: /launcher/ota_sources.json");
    show_items();
    if (!s_catalog->count && !ui_services_busy())
      submit(0, 0, NULL);
    lv_obj_t *pages = toolbar(s_page);
    tool_button(pages, "Pagina anterior", change_page, (void *)(intptr_t)-1);
    tool_button(pages, "Proxima pagina", change_page, (void *)1);
  }
  if (!s_timer)
    s_timer = lv_timer_create(poll, 250, NULL);
  return s_page;
}
lv_obj_t *ui_catalog_create(lv_obj_t *parent) { return create(parent, 0); }
lv_obj_t *ui_web_create(lv_obj_t *parent) { return create(parent, 1); }

bool ui_services_busy(void) { return atomic_load(&s_busy); }

lv_obj_t *ui_pi_create(lv_obj_t *parent) { return create(parent, 2); }
lv_obj_t *ui_partitions_create(lv_obj_t *parent) { return create(parent, 3); }

bool ui_services_power_safe(void) {
  pi_link_status_t p;
  pi_link_status(&p);
  return !p.usb && !ui_services_busy();
}
