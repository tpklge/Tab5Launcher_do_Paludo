#include "theme_catalog.h"
/* ui.c — UI root: init, navigation, LVGL task. */
#include "ui.h"
#include "board.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "input.h"
#include "storage.h"
#include "ui_nav.h"
#include "ui_visual.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

static ui_view_t s_current_view = UI_VIEW_BOOT;
static ui_theme_t s_theme = UI_THEME_GRAPHITE;
static uint32_t s_accent_hex = 0xEB2638; /* default blue */
static QueueHandle_t s_post_q = NULL;
static lv_obj_t *s_menu_buttons[UI_VIEW_COUNT];
extern bool ui_diag_handle_key(const key_event_t *key);
extern bool ui_wifi_handle_key(const key_event_t *key);
extern bool ui_sd_handle_key(const key_event_t *key);

typedef struct {
  void (*fn)(void *);
  void *arg;
} ui_post_item_t;

/* Declared in ui_theme.c */
extern void ui_menu_fonts(lv_obj_t *root);
extern void ui_theme_apply(ui_theme_t theme, uint32_t accent_hex);

/* Declared in ui_status_bar.c */
extern lv_obj_t *ui_status_bar_create(lv_obj_t *parent);
extern void ui_status_bar_update(void);

/* Declared in ui_home.c */
extern lv_obj_t *ui_home_create(lv_obj_t *parent);

/* Declared in ui_boot.c */
extern void ui_boot_start(lv_obj_t *parent, int slot_idx, uint8_t timeout_s);

/* Declared in wifi_mgr.h (lazy init when WiFi view first opened) */
extern void wifi_init(bool autoconnect);

/* Declared in view files */
extern lv_obj_t *ui_wifi_create(lv_obj_t *parent);
extern lv_obj_t *ui_sd_create(lv_obj_t *parent);
extern lv_obj_t *ui_cfg_create(lv_obj_t *parent);
extern lv_obj_t *ui_diag_create(lv_obj_t *parent);
extern lv_obj_t *ui_catalog_create(lv_obj_t *parent);
extern lv_obj_t *ui_web_create(lv_obj_t *parent);
extern lv_obj_t *ui_pi_create(lv_obj_t *parent);
extern lv_obj_t *ui_partitions_create(lv_obj_t *parent);

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_statusbar = NULL;
static lv_obj_t *s_sidemenu = NULL;
static lv_obj_t *s_content = NULL;

/* Side menu button click handler */
static void side_menu_btn_cb(lv_event_t *e) {
  ui_view_t v =
      (ui_view_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
  ui_navigate(v);
}

/* Post-queue processor: runs in LVGL task via lv_timer */
static void ui_post_timer_cb(lv_timer_t *t) {
  ui_post_item_t item;
  while (xQueueReceive(s_post_q, &item, 0) == pdTRUE) {
    if (item.fn)
      item.fn(item.arg);
  }
}

static void input_timer_cb(lv_timer_t *timer) {
  key_event_t key;
  for (int n = 0; n < 16 && input_get_key(&key); n++) {
    if (ui_visual_activity())
      continue;
    if (ui_diag_handle_key(&key) || ui_wifi_handle_key(&key) ||
        ui_sd_handle_key(&key))
      continue;
    if (!ui_nav_has_modal()) {
      if (key.kind == KEY_KIND_ESC || key.kind == KEY_KIND_F(6)) {
        ui_nav_focus(s_menu_buttons[s_current_view]);
        continue;
      }
      if (key.alt && key.kind == KEY_KIND_CHAR) {
        static const ui_view_t views[] = {
            UI_VIEW_HOME, UI_VIEW_SD, UI_VIEW_WIFI, UI_VIEW_CFG, UI_VIEW_DIAG};
        if (key.ch >= '1' && key.ch <= '5') {
          ui_navigate(views[key.ch - '1']);
          continue;
        }
      }
    }
    uint32_t code = 0;
    switch (key.kind) {
    case KEY_KIND_ENTER:
      code = LV_KEY_ENTER;
      break;
    case KEY_KIND_ESC:
      code = LV_KEY_ESC;
      break;
    case KEY_KIND_TAB:
      code = key.shift ? LV_KEY_PREV : LV_KEY_NEXT;
      break;
    case KEY_KIND_LEFT:
      code = LV_KEY_LEFT;
      break;
    case KEY_KIND_RIGHT:
      code = LV_KEY_RIGHT;
      break;
    case KEY_KIND_UP:
      code = LV_KEY_UP;
      break;
    case KEY_KIND_DOWN:
      code = LV_KEY_DOWN;
      break;
    case KEY_KIND_BACKSPACE:
      code = LV_KEY_BACKSPACE;
      break;
    case KEY_KIND_CHAR:
      code = key_char(&key);
      break;
    default:
      break;
    }
    if (code)
      ui_nav_send_key(code);
  }
}

static void status_timer_cb(lv_timer_t *timer) { ui_status_bar_update(); }

/* ---- Side menu ----------------------------------------------------------- */

static lv_obj_t *create_side_menu(lv_obj_t *parent) {
  lv_obj_t *menu = lv_obj_create(parent);
  lv_obj_set_size(menu, 112, LV_PCT(100));
  lv_obj_align(menu, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(menu, lv_color_hex(0x191C21), 0);
  lv_obj_set_style_border_width(menu, 0, 0);
  lv_obj_set_style_pad_all(menu, 8, 0);
  lv_obj_set_flex_flow(menu, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *brand = lv_label_create(menu);
  lv_label_set_text(brand, "TAB5");
  lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(brand, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_pad_ver(brand, 16, 0);
  static const struct {
    const char *label;
    ui_view_t view;
  } items[] = {
      {LV_SYMBOL_HOME "\nHOME", UI_VIEW_HOME},
      {LV_SYMBOL_WIFI "\nWi-Fi", UI_VIEW_WIFI},
      {LV_SYMBOL_SD_CARD "\nSD", UI_VIEW_SD},
      {LV_SYMBOL_DOWNLOAD "\nOTA", UI_VIEW_OTA},
      {LV_SYMBOL_GPS "\nWEB", UI_VIEW_WEBUI},
      {LV_SYMBOL_DRIVE "\nFLASH", UI_VIEW_PMAN},
      {LV_SYMBOL_USB "\nPI", UI_VIEW_PI},
      {LV_SYMBOL_SETTINGS "\nCFG", UI_VIEW_CFG},
      {LV_SYMBOL_LIST "\nDIAG", UI_VIEW_DIAG},
  };

  for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++) {
    lv_obj_t *btn = lv_button_create(menu);
    lv_obj_set_size(btn, 94, 70);
    s_menu_buttons[items[i].view] = btn;
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x611620), LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xEB2638),
                                  LV_STATE_CHECKED);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xEB2638),
                                  LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x24282F), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, items[i].label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    /* Store view index in user_data */
    lv_obj_set_user_data(btn, (void *)(intptr_t)items[i].view);
    lv_obj_add_event_cb(btn, side_menu_btn_cb, LV_EVENT_CLICKED, NULL);
  }
  return menu;
}

/* ---- Init ---------------------------------------------------------------- */

#ifdef TAB5_UI_NAV_SELFTEST
/* Optional diagnostic build: only opens views, never writes user files/flash.
 */
static void navigation_selftest(lv_timer_t *timer) {
  static unsigned step;
  if (step == 10) {
    ESP_LOGI("ui_selftest",
             "complete: 5 SD visits; LVGL stack minimum free=%u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
    lv_timer_delete(timer);
    return;
  }
  ui_view_t view = (step % 2) == 0 ? UI_VIEW_SD : UI_VIEW_HOME;
  ESP_LOGI("ui_selftest",
           "step %u/10: view %s; LVGL stack minimum free=%u bytes", step + 1,
           view == UI_VIEW_SD ? "SD" : "HOME",
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
  ++step;
  ui_navigate(view);
}
#endif

esp_err_t ui_init(void) {
  s_post_q = xQueueCreate(16, sizeof(ui_post_item_t));
  if (!s_post_q)
    return ESP_ERR_NO_MEM;

  if (!board_lvgl_lock(2000)) {
    ESP_LOGE(TAG, "LVGL lock timeout");
    return ESP_FAIL;
  }

  const launcher_config_t *cfg = storage_get_config();

  if (strlen(cfg->color) == 6) {
    unsigned parsed = 0;
    if (sscanf(cfg->color, "%6x", &parsed) == 1)
      s_accent_hex = parsed;
  }
  s_theme = (ui_theme_t)launcher_theme_index(cfg->theme);
  ui_theme_apply(s_theme, s_accent_hex);

  /* Root screen */
  s_root = lv_screen_active();
  ui_nav_init(s_root);
  lv_obj_set_style_bg_color(s_root, lv_color_hex(0x101318), 0);
  lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);

  /* Status bar at top */
  s_statusbar = ui_status_bar_create(s_root);

  /* Side menu */
  s_sidemenu = create_side_menu(s_root);

  /* Content area — use actual display dimensions after rotation */
  lv_coord_t disp_w =
      (lv_coord_t)lv_display_get_horizontal_resolution(board_display());
  lv_coord_t disp_h =
      (lv_coord_t)lv_display_get_vertical_resolution(board_display());
  s_content = lv_obj_create(s_root);
  lv_obj_set_pos(s_content, 112,
                 40); /* after 64px side menu, below 32px status bar */
  lv_obj_set_size(s_content, disp_w - 112, disp_h - 40);
  lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(s_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_content, 0, 0);
  lv_obj_set_style_pad_all(s_content, 0, 0);

  /* Post-queue timer */
  lv_timer_create(ui_post_timer_cb, 20, NULL);
  lv_timer_create(input_timer_cb, 20, NULL);

#ifdef TAB5_UI_NAV_SELFTEST
  lv_timer_create(navigation_selftest, 3000, NULL);
#endif
  /* Status bar refresh every second */
  lv_timer_create(status_timer_cb, 1000, NULL);

  ui_visual_init(s_root);
  /* Show home view */
  ui_home_create(s_content);
  s_current_view = UI_VIEW_HOME;
  lv_obj_add_state(s_menu_buttons[UI_VIEW_HOME], LV_STATE_CHECKED);
  ui_nav_refresh();
  ui_nav_focus(s_menu_buttons[UI_VIEW_HOME]);

  board_lvgl_unlock();

  board_reveal(cfg->bright);

  ESP_LOGI(TAG, "ui_init done (display %dx%d)", (int)disp_w, (int)disp_h);
  return ESP_OK;
}

static void navigate_apply(void *arg) {
  ui_view_t view = (ui_view_t)(intptr_t)arg;
  if (!board_lvgl_lock(500))
    return;
  lv_obj_clean(s_content);
  s_current_view = view;
  for (int i = 0; i < UI_VIEW_COUNT; i++) {
    if (!s_menu_buttons[i])
      continue;
    if (i == view)
      lv_obj_add_state(s_menu_buttons[i], LV_STATE_CHECKED);
    else
      lv_obj_clear_state(s_menu_buttons[i], LV_STATE_CHECKED);
  }

  switch (view) {
  case UI_VIEW_HOME:
    ui_home_create(s_content);
    break;
  case UI_VIEW_BOOT:
    /* handled by ui_bootscreen_run() */
    break;
  case UI_VIEW_WIFI:
    /* Lazy-init Wi-Fi (C6 / ESP-Hosted) on first use */
    wifi_init(storage_get_config()->wifi_autoconnect);
    ui_wifi_create(s_content);
    break;
  case UI_VIEW_SD:
    ui_sd_create(s_content);
    break;
  case UI_VIEW_OTA:
    ui_catalog_create(s_content);
    break;
  case UI_VIEW_WEBUI:
    ui_web_create(s_content);
    break;
  case UI_VIEW_PI:
    ui_pi_create(s_content);
    break;
  case UI_VIEW_PMAN:
    ui_partitions_create(s_content);
    break;
  case UI_VIEW_CFG:
    ui_cfg_create(s_content);
    break;
  case UI_VIEW_DIAG:
    ui_diag_create(s_content);
    break;
  default: {
    lv_obj_t *lbl = lv_label_create(s_content);
    lv_label_set_text_fmt(lbl, "View %d\n(not yet implemented)", (int)view);
    lv_obj_center(lbl);
  } break;
  }

  ui_theme_apply(s_theme, s_accent_hex);
  ui_menu_fonts(s_content);
  ui_nav_refresh();
  ui_nav_focus_first(s_content);
  board_lvgl_unlock();
}

void ui_navigate(ui_view_t view) {
  /* Defer deletion until the current input event has returned. */
  if (view >= 0 && view < UI_VIEW_COUNT)
    ui_post(navigate_apply, (void *)(intptr_t)view);
}

void ui_set_theme(ui_theme_t theme) {
  s_theme = theme;
  if (!board_lvgl_lock(500))
    return;
  ui_theme_apply(theme, s_accent_hex);
  ui_visual_reload();
  board_lvgl_unlock();
}

ui_theme_t ui_get_theme(void) { return s_theme; }

void ui_set_accent(uint32_t hex) {
  s_accent_hex = hex;
  if (!board_lvgl_lock(500))
    return;
  ui_theme_apply(s_theme, hex);
  board_lvgl_unlock();
}

void ui_bootscreen_run(int slot_idx, uint8_t timeout_s) {
#ifdef TAB5_UI_NAV_SELFTEST
  return; /* diagnostic navigation must not automatically launch an app */
#endif
  if (!board_lvgl_lock(500))
    return;
  lv_obj_clean(s_content);
  s_current_view = UI_VIEW_BOOT;
  ui_boot_start(s_content, slot_idx, timeout_s);
  board_lvgl_unlock();
}

void ui_status_bar_refresh(void) {
  if (!board_lvgl_lock(100))
    return;
  ui_status_bar_update();
  board_lvgl_unlock();
}

void ui_post(void (*fn)(void *), void *arg) {
  ui_post_item_t item = {.fn = fn, .arg = arg};
  if (s_post_q)
    xQueueSend(s_post_q, &item, 0);
}

static bool s_style_pending;
static void style_apply(void *unused) {
  s_style_pending = false;
  ui_theme_apply(s_theme, s_accent_hex);
  ui_menu_fonts(s_content);
}
void ui_style_refresh(void) {
  if (!s_style_pending && lv_async_call(style_apply, NULL) == LV_RESULT_OK)
    s_style_pending = true;
}

static void refresh_config(void *unused) {
  const launcher_config_t *c = storage_get_config();
  ui_set_theme((ui_theme_t)launcher_theme_index(c->theme));
  unsigned color;
  if (strlen(c->color) == 6 && sscanf(c->color, "%6x", &color) == 1)
    ui_set_accent(color);
  ui_visual_reload();
  if (s_current_view == UI_VIEW_CFG)
    ui_navigate(UI_VIEW_CFG);
}
void ui_request_config_refresh(void) { ui_post(refresh_config, NULL); }
