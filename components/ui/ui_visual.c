#include "ui_visual.h"
#ifdef ESP_PLATFORM
#include "diagnostics.h"
#endif
#include "board.h"
#include "installer.h"
#include "storage.h"
#include "time_mgr.h"
#include "ui.h"
#include "ui_assets.h"
#include "ui_nav.h"
#include "ui_progress.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern bool ui_sd_busy(void);
static lv_obj_t *s_root, *s_wall, *s_overlay, *s_clock, *s_date, *s_hands[2];
static lv_point_precise_t s_points[2][2];
static bool s_dimmed, s_off;
static lv_font_t s_date_font;
static int s_hand_minute = -1;
static void overlay_gone(lv_event_t *e) {
  s_overlay = s_clock = s_date = NULL;
  s_hands[0] = s_hands[1] = NULL;
  s_hand_minute = -1;
}
bool ui_visual_activity(void) {
  bool consumed = s_overlay || s_off;
  if (s_overlay)
    lv_obj_delete(s_overlay);
  if (s_dimmed || s_off)
    board_backlight(storage_get_config()->bright);
  s_dimmed = s_off = false;
#ifdef ESP_PLATFORM
  diagnostics_idle_checkpoint(0, 0);
#endif
  lv_display_trigger_activity(board_display());
  return consumed;
}
static void touch_wake(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (indev) {
    lv_indev_wait_release(indev);
    /* In IRQ mode deleting the pressed overlay returns from LVGL before its
     * normal release polling is armed. Read until release, even if the panel
     * only interrupts on touch-down. LVGL pauses this timer on release. */
    lv_timer_t *read = lv_indev_get_read_timer(indev);
    if (read && lv_indev_get_mode(indev) == LV_INDEV_MODE_EVENT)
      lv_timer_resume(read);
  }
  ui_visual_activity();
}
static void clock_update(void) {
  if (!s_clock)
    return;
  char text[80];
  if (!time_is_valid())
    snprintf(text, sizeof(text), "Hora nao ajustada");
  else
    time_fmt_hhmm(text, sizeof(text));
  if (strcmp(lv_label_get_text(s_clock), text))
    lv_label_set_text(s_clock, text);
  if (s_date) {
    time_fmt_rest(text, sizeof(text));
    const char *date = time_is_valid() ? text : "Ajuste data e hora em CFG";
    if (strcmp(lv_label_get_text(s_date), date))
      lv_label_set_text(s_date, date);
  }
  if (s_hands[0] && time_is_valid()) {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    int minute = t.tm_hour * 60 + t.tm_min;
    if (s_hand_minute == minute)
      return;
    s_hand_minute = minute;
    double angles[] = {((t.tm_hour % 12) + t.tm_min / 60.0) * M_PI / 6 -
                           M_PI / 2,
                       t.tm_min * M_PI / 30 - M_PI / 2};
    for (int i = 0; i < 2; i++) {
      int length = i ? 115 : 80;
      s_points[i][0] = (lv_point_precise_t){150, 150};
      s_points[i][1] = (lv_point_precise_t){150 + cos(angles[i]) * length,
                                            150 + sin(angles[i]) * length};
      lv_line_set_points(s_hands[i], s_points[i], 2);
    }
  }
}
static void overlay_open(void) {
#ifdef ESP_PLATFORM
  diagnostics_idle_checkpoint(lv_display_get_inactive_time(board_display()) / 1000, 2);
#endif
  const launcher_config_t *cfg = storage_get_config();
  s_overlay = lv_obj_create(lv_layer_top());
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_USER_1);
  lv_obj_set_pos(s_overlay, 0, 0);
  lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_overlay,
                          cfg->clock_back == 1 ? LV_OPA_50 : LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_overlay, 0, 0);
  lv_obj_set_style_radius(s_overlay, 0, 0);
  lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_overlay, overlay_gone, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(s_overlay, touch_wake, LV_EVENT_PRESSED, NULL);
  ui_nav_modal(s_overlay);
  if (cfg->idle_clock) {
    if (cfg->clock_back == 0 && cfg->wallpaper[0] && cfg->wallpaper[0] != '-') {
      lv_obj_t *image = lv_image_create(s_overlay);
      lv_obj_center(image);
      lv_obj_set_style_image_opa(image, LV_OPA_50, 0);
      ui_assets_set_blur(image, cfg->wallpaper, 1280, 720, 1280, 720);
    }
    s_clock = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_clock, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_clock,
                               cfg->clock_style == 2 ? &lv_font_montserrat_24
                                                     : &lv_font_montserrat_32,
                               0);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, cfg->clock_style == 3 ? 95 : -80);
    lv_obj_set_style_transform_pivot_x(s_clock, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(s_clock, LV_PCT(50), 0);
    if (cfg->clock_style != 3) {
      lv_obj_set_style_transform_scale_x(s_clock,
                                         cfg->clock_style == 1 ? 768 : 512, 0);
      lv_obj_set_style_transform_scale_y(s_clock,
                                         cfg->clock_style == 1 ? 768 : 512, 0);
    }
    if (cfg->clock_style == 3) {
      lv_obj_t *face = lv_obj_create(s_overlay);
      lv_obj_set_size(face, 304, 304);
      lv_obj_align(face, LV_ALIGN_CENTER, 0, -95);
      lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(face, LV_OPA_50, 0);
      lv_obj_set_style_border_color(face, lv_color_white(), 0);
      lv_obj_clear_flag(face, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_pad_all(face, 0, 0);
      for (int i = 0; i < 2; i++) {
        s_hands[i] = lv_line_create(face);
        lv_obj_set_style_line_color(s_hands[i], lv_color_white(), 0);
        lv_obj_set_style_line_width(s_hands[i], i ? 4 : 7, 0);
      }
    }
    s_date = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_date, lv_color_white(), 0);
    LV_FONT_DECLARE(ui_latin20);
    s_date_font = lv_font_montserrat_20;
    s_date_font.fallback = &ui_latin20;
    lv_obj_set_style_text_font(s_date, &s_date_font, 0);
    lv_obj_set_style_transform_pivot_x(s_date, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(s_date, LV_PCT(50), 0);
    lv_obj_set_style_text_align(s_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_date, 8, 0);
    lv_obj_set_style_transform_scale_x(s_date, 512, 0);
    lv_obj_set_style_transform_scale_y(s_date, 512, 0);
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, cfg->clock_style == 3 ? 225 : 95);
    clock_update();
  }
}
static void timer(lv_timer_t *t) {
  if (installer_is_busy() || ui_sd_busy() || ui_services_busy() ||
      ui_progress_visible()) {
    ui_visual_activity();
    return;
  }
  const launcher_config_t *c = storage_get_config();
  uint32_t idle = lv_display_get_inactive_time(board_display()) / 1000;
#ifdef ESP_PLATFORM
  diagnostics_idle_checkpoint(idle, s_off ? 3 : s_overlay ? 2 : s_dimmed ? 1 : 0);
#endif
  if (!idle && (s_dimmed || s_off))
    ui_visual_activity();
  if (c->dim_s && idle >= c->dim_s && !s_dimmed) {
    board_backlight(c->bright > 20 ? 20 : c->bright);
    s_dimmed = true;
    if (c->idle_clock && !ui_nav_has_modal())
      overlay_open();
  }
  if (c->screen_off_s && idle >= c->screen_off_s && !s_off) {
    if (!s_overlay && !ui_nav_has_modal())
      overlay_open();
    if (s_overlay) {
      board_backlight(0);
      s_off = true;
    }
  }
  /* Dark screens do not need transformed labels/layers redrawn each minute. */
  if (!s_off) clock_update();
}
void ui_visual_reload(void) {
  if (!s_root)
    return;
  if (s_wall) {
    lv_obj_delete(s_wall);
    s_wall = NULL;
  }
  const char *path = storage_get_config()->wallpaper;
  if (path[0] && path[0] != '-') {
    s_wall = lv_image_create(s_root);
    lv_obj_center(s_wall);
    lv_obj_move_background(s_wall);
    lv_obj_set_style_image_opa(s_wall,
                               storage_get_config()->wall_dim == 2 ? LV_OPA_30
                               : storage_get_config()->wall_dim == 1
                                   ? LV_OPA_50
                                   : LV_OPA_80,
                               0);
    ui_assets_set(s_wall, path, 1280, 720, 1280, 720);
  }
}
void ui_visual_init(lv_obj_t *root) {
  s_root = root;
  ui_assets_init();
  ui_visual_reload();
  lv_timer_create(timer, 250, NULL);
}
