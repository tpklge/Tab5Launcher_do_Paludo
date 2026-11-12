#pragma once
/* ui — LVGL-based launcher interface.
 *
 * Visual design inspired by UNIZERSION tab5-launcher: side menu, app carousel, depth/shadow,
 * wallpaper, nine built-in themes, status bar.
 *
 * LVGL is driven by the esp_lvgl_port task.  All lv_* calls must be made with
 * board_lvgl_lock() held.  Worker tasks post events via ui_post_event().
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_VIEW_BOOT = 0,
    UI_VIEW_HOME,
    UI_VIEW_WIFI,
    UI_VIEW_SD,
    UI_VIEW_OTA,
    UI_VIEW_WEBUI,
    UI_VIEW_PMAN,
    UI_VIEW_CFG,
    UI_VIEW_DIAG,
    UI_VIEW_PI,
    UI_VIEW_COUNT,
} ui_view_t;

typedef enum {
    UI_THEME_GRAPHITE = 0,
    UI_THEME_MIDNIGHT,
    UI_THEME_DAYLIGHT,
    UI_THEME_NEON,
    UI_THEME_OCEAN,
    UI_THEME_FOREST,
    UI_THEME_SUNSET,
    UI_THEME_LAVENDER,
    UI_THEME_SAND,
    UI_THEME_COUNT,
} ui_theme_t;

/* Colors per theme */
typedef struct {
    lv_color_t bg;          /* main background */
    lv_color_t panel;       /* card/panel background */
    lv_color_t text;        /* primary text */
    lv_color_t text_dim;    /* secondary text */
    lv_color_t accent;      /* user-chosen accent color */
    lv_color_t border;      /* panel border */
    lv_color_t shadow;      /* drop-shadow tint */
} ui_theme_colors_t;

/* Initialize LVGL UI.  Requires board_init() already done.
 * Creates the LVGL port task, sets up display and touch indev.
 * Returns ESP_OK or ESP_FAIL. */
esp_err_t ui_init(void);
bool ui_sd_prepare_worker(void);
bool ui_services_prepare_worker(void);

/* Navigate to a view (safe to call without LVGL lock — posts to LVGL task). */
void ui_navigate(ui_view_t view);

/* Apply theme (live, with LVGL lock). */
void ui_set_theme(ui_theme_t theme);
ui_theme_t ui_get_theme(void);

/* Set accent color (hex RGB, e.g. 0x3498DB for blue). */
void ui_set_accent(uint32_t hex);

/* Boot screen: start countdown for slot `slot_idx` with `timeout_s` seconds.
 * Calls boot_mgr_launch_slot() on completion or returns if the user stays. */
void ui_bootscreen_run(int slot_idx, uint8_t timeout_s);

/* Status bar update (called by ticker task periodically). */
void ui_status_bar_refresh(void);

/* Post a function to run with the LVGL lock held (from any task). */
void ui_post(void (*fn)(void *), void *arg);

#ifdef __cplusplus
}
#endif

/* Restyle current tree after asynchronous widget creation (LVGL lock held). */
void ui_style_refresh(void);
lv_color_t ui_text_color(void);

bool ui_services_busy(void);
bool ui_sd_busy(void);

bool ui_services_power_safe(void);

void ui_request_config_refresh(void);
