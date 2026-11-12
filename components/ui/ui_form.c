#include "ui_form.h"
#include "ui_nav.h"
static lv_obj_t *s_modal, *s_field, *s_keyboard;
static ui_form_save_fn s_save;
static void *s_user;
static void gone(lv_event_t *e) {
  s_modal = s_field = s_keyboard = NULL;
  s_save = NULL;
  s_user = NULL;
}
void ui_form_close(void) {
  if (s_modal)
    lv_obj_delete(s_modal);
}
static void close_cb(lv_event_t *e) { ui_form_close(); }
static void keyboard_cb(lv_event_t *e) {
  if (s_keyboard) {
    if (lv_obj_has_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN))
      lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}
static void save_cb(lv_event_t *e) {
  if (s_field && s_save && s_save(lv_textarea_get_text(s_field), s_user))
    lv_obj_delete_async(s_modal);
}
static lv_obj_t *button(lv_obj_t *p, const char *text, int x,
                        lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_size(b, 200, 44);
  lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -12);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  return b;
}
void ui_form_open(const char *title, const char *initial, bool multiline,
                  unsigned max_length, ui_form_save_fn save, void *user) {
  ui_form_close();
  s_save = save;
  s_user = user;
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_style_text_font(s_modal, &lv_font_montserrat_20, 0);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_modal, 0, 0);
  lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
  lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
  lv_obj_set_style_border_width(s_modal, 0, 0);
  ui_nav_modal(s_modal);
  lv_obj_add_event_cb(s_modal, gone, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(s_modal, close_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_t *panel = lv_obj_create(s_modal);
  lv_obj_set_size(panel, 1000, 640);
  lv_obj_center(panel);
  lv_obj_t *l = lv_label_create(panel);
  lv_label_set_text(l, title);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  s_field = lv_textarea_create(panel);
  lv_obj_set_pos(s_field, 0, 54);
  lv_obj_set_size(s_field, 950, multiline ? 220 : 60);
  lv_obj_set_style_text_font(s_field, &lv_font_montserrat_24, 0);
  lv_textarea_set_one_line(s_field, !multiline);
  lv_textarea_set_max_length(s_field, max_length);
  lv_textarea_set_text(s_field, initial ? initial : "");
  if (!multiline)
    lv_obj_add_event_cb(s_field, save_cb, LV_EVENT_READY, NULL);
  s_keyboard = lv_keyboard_create(panel);
  lv_obj_set_size(s_keyboard, 950, 210);
  lv_obj_set_pos(s_keyboard, 0, 286);
  lv_keyboard_set_textarea(s_keyboard, s_field);
  lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_READY, NULL);
  button(panel, "Cancelar / Esc", -260, close_cb);
  button(panel, "Teclado virtual", 0, keyboard_cb);
  lv_obj_t *save_button = button(panel, "Salvar", 260, save_cb);
  lv_obj_add_flag(save_button, LV_OBJ_FLAG_USER_2);
  ui_nav_refresh();
  ui_nav_focus(s_field);
}
void ui_notice(const char *title, const char *message) {
  ui_form_close();
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_set_style_text_font(s_modal, &lv_font_montserrat_20, 0);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_modal, 0, 0);
  ui_nav_modal(s_modal);
  lv_obj_add_event_cb(s_modal, gone, LV_EVENT_DELETE, NULL);
  lv_obj_add_event_cb(s_modal, close_cb, LV_EVENT_CANCEL, NULL);
  lv_obj_t *panel = lv_obj_create(s_modal);
  lv_obj_set_size(panel, 900, 380);
  lv_obj_center(panel);
  lv_obj_t *l = lv_label_create(panel);
  lv_label_set_text(l, title);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
  l = lv_label_create(panel);
  lv_obj_set_pos(l, 0, 60);
  lv_obj_set_width(l, 840);
  lv_label_set_text(l, message);
  lv_obj_t *close = button(panel, "Fechar / Esc", 0, close_cb);
  ui_nav_refresh();
  ui_nav_focus(close);
}

static void (*s_confirm)(void *);
static void confirm_cb(lv_event_t *e) {
  void (*cb)(void *) = s_confirm;
  void *user = s_user;
  ui_form_close();
  if (cb)
    cb(user);
}
void ui_confirm(const char *title, const char *message, const char *accept,
                void (*cb)(void *), void *user) {
  ui_notice(title, message);
  s_confirm = cb;
  s_user = user;
  lv_obj_t *panel = lv_obj_get_child(s_modal, 0);
  lv_obj_t *cancel = lv_obj_get_child(panel, 2);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, -220, -12);
  button(panel, accept, 220, confirm_cb);
  ui_nav_refresh();
  ui_nav_focus(cancel);
}

#include <stdio.h>
#include <string.h>
static lv_obj_t *s_date, *s_time, *s_date_error;
static bool (*s_date_save)(time_t, void *);
static bool masking;
static void mask_changed(lv_event_t *e) {
  if (masking)
    return;
  lv_obj_t *o = lv_event_get_target(e);
  const char *text = lv_textarea_get_text(o);
  char digits[9] = {0}, formatted[12] = {0};
  unsigned n = 0, k = 0, limit = o == s_date ? 8 : 6;
  unsigned cursor = lv_textarea_get_cursor_pos(o), before = 0;
  for (unsigned i = 0; text[i]; i++)
    if (text[i] >= '0' && text[i] <= '9' && n < limit) {
      digits[n++] = text[i];
      if (i < cursor)
        before++;
    }
  unsigned position = 0;
  for (unsigned i = 0; i < n; i++) {
    if (i == 2 || i == 4)
      formatted[k++] = o == s_date ? '/' : ':';
    if (i == before)
      position = k;
    formatted[k++] = digits[i];
  }
  if (before >= n)
    position = k;
  if (strcmp(text, formatted)) {
    masking = true;
    lv_textarea_set_text(o, formatted);
    lv_textarea_set_cursor_pos(o, position);
    masking = false;
  }
}
static void date_save_cb(lv_event_t *e) {
  int d, m, y, h, min, sec, n = 0;
  const char *date = lv_textarea_get_text(s_date),
             *clock = lv_textarea_get_text(s_time);
  bool ok = strlen(date) == 10 && strlen(clock) == 8 &&
            sscanf(date, "%2d/%2d/%4d%n", &d, &m, &y, &n) == 3 && n == 10;
  n = 0;
  ok = ok && sscanf(clock, "%2d:%2d:%2d%n", &h, &min, &sec, &n) == 3 && n == 8;
  if (ok) {
    ok = y >= 2000 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= 31 &&
         h >= 0 && h <= 23 && min >= 0 && min <= 59 && sec >= 0 && sec <= 59;
  }
  if (ok) {
    struct tm t = {.tm_year = y - 1900,
                   .tm_mon = m - 1,
                   .tm_mday = d,
                   .tm_hour = h,
                   .tm_min = min,
                   .tm_sec = sec,
                   .tm_isdst = -1};
    time_t value = mktime(&t);
    ok = t.tm_year == y - 1900 && t.tm_mon == m - 1 && t.tm_mday == d &&
         t.tm_hour == h && t.tm_min == min && t.tm_sec == sec;
    if (ok && s_date_save && s_date_save(value, s_user)) {
      lv_obj_delete_async(s_modal);
      return;
    }
  }
  lv_label_set_text(s_date_error,
                    "Data ou hora inválida. Use DD/MM/AAAA e HH:MM:SS.");
}
static void date_focus(lv_event_t *e) {
  if (s_keyboard)
    lv_keyboard_set_textarea(s_keyboard, lv_event_get_target(e));
}
void ui_datetime_open(bool (*fn)(time_t, void *), void *user) {
  ui_form_open("Data e hora local", "", false, 10, NULL, user);
  s_date_save = fn;
  lv_obj_t *panel = lv_obj_get_parent(s_field);
  s_date = s_field;
  lv_obj_set_size(s_date, 440, 60);
  lv_textarea_set_placeholder_text(s_date, "DD/MM/AAAA");
  s_time = lv_textarea_create(panel);
  lv_obj_set_pos(s_time, 480, 54);
  lv_obj_set_size(s_time, 440, 60);
  lv_textarea_set_one_line(s_time, true);
  lv_textarea_set_max_length(s_time, 8);
  lv_obj_set_style_text_font(s_time, &lv_font_montserrat_24, 0);
  lv_textarea_set_placeholder_text(s_time, "HH:MM:SS");
  time_t now = time(NULL);
  struct tm t;
  localtime_r(&now, &t);
  char text[24];
  strftime(text, sizeof(text), "%d/%m/%Y", &t);
  lv_textarea_set_text(s_date, text);
  strftime(text, sizeof(text), "%H:%M:%S", &t);
  lv_textarea_set_text(s_time, text);
  lv_obj_add_event_cb(s_date, mask_changed, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(s_time, mask_changed, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(s_date, date_focus, LV_EVENT_FOCUSED, NULL);
  lv_obj_add_event_cb(s_time, date_focus, LV_EVENT_FOCUSED, NULL);
  s_date_error = lv_label_create(panel);
  lv_obj_set_pos(s_date_error, 0, 140);
  lv_label_set_text(s_date_error, "Data: DD/MM/AAAA     Hora: HH:MM:SS");
  lv_obj_t *b = lv_obj_get_child(panel, -1);
  (void)b;
  /* Replace the save action while keeping the physical keyboard group. */
  for (unsigned i = 0; i < lv_obj_get_child_count(panel); i++) {
    lv_obj_t *o = lv_obj_get_child(panel, i);
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_2)) {
      lv_obj_remove_event_cb(o, save_cb);
      lv_obj_add_event_cb(o, date_save_cb, LV_EVENT_CLICKED, NULL);
    }
  }
  lv_obj_add_event_cb(s_time, date_save_cb, LV_EVENT_READY, NULL);
  ui_nav_refresh();
  ui_nav_focus(s_date);
}
lv_obj_t *ui_form_panel(void) {
  return s_modal ? lv_obj_get_child(s_modal, 0) : NULL;
}
