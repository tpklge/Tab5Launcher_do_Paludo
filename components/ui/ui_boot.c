/* ui_boot.c — Boot screen: circular countdown, LAUNCHER / START NOW buttons. */
#include "ui.h"
#include "ui_nav.h"
#include "ui_form.h"
#include "board.h"
#include "boot_mgr.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_boot";

static lv_obj_t   *s_arc     = NULL;
static lv_obj_t   *s_lbl_app = NULL;
static lv_obj_t   *s_lbl_cnt = NULL;
static lv_timer_t *s_timer   = NULL;
static int         s_slot_idx = -1;
static int         s_remaining = 0;  /* seconds left */

static void launch_error(void *arg) { char b[100]; snprintf(b,sizeof(b),"Falha ao iniciar: %s",esp_err_to_name((esp_err_t)(intptr_t)arg)); ui_notice("BOOT",b); }
static void launch_done(esp_err_t err,void *user) { if(err!=ESP_OK) ui_post(launch_error,(void*)(intptr_t)err); }
static void boot_delete_cb(lv_event_t *e)
{
    if (s_timer) { lv_timer_delete(s_timer); s_timer = NULL; }
    s_arc = s_lbl_app = s_lbl_cnt = NULL;
}

static void countdown_timer_cb(lv_timer_t *t)
{
    s_remaining--;
    if (s_remaining <= 0) {
        lv_timer_del(s_timer);
        s_timer = NULL;
        /* Launch the app */
        if (s_slot_idx >= 0) {
            ESP_LOGI(TAG, "autoboot slot %d", s_slot_idx);
            boot_mgr_launch_async(s_slot_idx,launch_done,NULL);  /* does not return on success */
        }
        ui_navigate(UI_VIEW_HOME);
        return;
    }

    /* Update arc angle: 360° → 0° over timeout */
    int8_t timeout = boot_mgr_get_timeout();
    if (timeout < 1) timeout = 1;
    int32_t angle = (int32_t)s_remaining * 360 / timeout;
    lv_arc_set_value(s_arc, (int32_t)(angle * 100 / 360));  /* arc range 0–100 */

    char buf[8];
    snprintf(buf, sizeof(buf), "%d", s_remaining);
    lv_label_set_text(s_lbl_cnt, buf);
}

static void btn_launcher_cb(lv_event_t *e)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    ui_navigate(UI_VIEW_HOME);
}

static void btn_start_now_cb(lv_event_t *e)
{
    if (s_timer) { lv_timer_del(s_timer); s_timer = NULL; }
    if (s_slot_idx >= 0) {
        boot_mgr_launch_async(s_slot_idx,launch_done,NULL);
    }
}

void ui_boot_start(lv_obj_t *parent, int slot_idx, uint8_t timeout_s)
{
    s_slot_idx  = slot_idx;
    s_remaining = timeout_s;

    /* Background */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_add_event_cb(bg, boot_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0D0D1A), 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    /* App name label */
    s_lbl_app = lv_label_create(bg);
    char app_name[49] = "No app";
    if (slot_idx >= 0) {
        const esp_partition_t *p = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot_idx),
            NULL);
        if (p) {
            esp_app_desc_t desc;
            if (esp_ota_get_partition_description(p, &desc) == ESP_OK) {
                strncpy(app_name, desc.project_name, sizeof(app_name) - 1);
            }
        }
    }
    lv_label_set_text(s_lbl_app, app_name);
    lv_obj_set_style_text_font(s_lbl_app, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_lbl_app, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_lbl_app, LV_ALIGN_TOP_MID, 0, 80);

    /* Countdown arc */
    s_arc = lv_arc_create(bg);
    lv_obj_set_size(s_arc, 200, 200);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -20);
    lv_arc_set_range(s_arc, 0, 100);
    lv_arc_set_value(s_arc, 100);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x5B5BFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, lv_color_hex(0x2A2A3E), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 8, LV_PART_MAIN);
    lv_arc_set_mode(s_arc, LV_ARC_MODE_NORMAL);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);  /* hide knob */

    /* Countdown number inside arc */
    s_lbl_cnt = lv_label_create(bg);
    char cnt_buf[4];
    snprintf(cnt_buf, sizeof(cnt_buf), "%d", s_remaining);
    lv_label_set_text(s_lbl_cnt, cnt_buf);
    lv_obj_set_style_text_font(s_lbl_cnt, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_lbl_cnt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align_to(s_lbl_cnt, s_arc, LV_ALIGN_CENTER, 0, 0);

    /* LAUNCHER button */
    lv_obj_t *btn_lnch = lv_button_create(bg);
    lv_obj_set_size(btn_lnch, 200, 48);
    lv_obj_align(btn_lnch, LV_ALIGN_BOTTOM_MID, -120, -40);
    lv_obj_set_style_bg_color(btn_lnch, lv_color_hex(0x2C2C3E), 0);
    lv_obj_set_style_radius(btn_lnch, 8, 0);
    lv_obj_t *llbl = lv_label_create(btn_lnch);
    lv_label_set_text(llbl, "LAUNCHER");
    lv_obj_center(llbl);
    lv_obj_add_event_cb(btn_lnch, btn_launcher_cb, LV_EVENT_CLICKED, NULL);

    /* START NOW button */
    lv_obj_t *btn_now = lv_button_create(bg);
    lv_obj_add_flag(btn_now,LV_OBJ_FLAG_USER_2);
    lv_obj_set_size(btn_now, 200, 48);
    lv_obj_align(btn_now, LV_ALIGN_BOTTOM_MID, 120, -40);
    lv_obj_set_style_bg_color(btn_now, lv_color_hex(0x5B5BFF), 0);
    lv_obj_set_style_radius(btn_now, 8, 0);
    lv_obj_t *nlbl = lv_label_create(btn_now);
    lv_label_set_text(nlbl, "START NOW");
    lv_obj_center(nlbl);
    lv_obj_add_event_cb(btn_now, btn_start_now_cb, LV_EVENT_CLICKED, NULL);

    ui_nav_modal(bg);
    lv_obj_add_event_cb(bg,btn_launcher_cb,LV_EVENT_CANCEL,NULL);
    ui_nav_refresh(); ui_nav_focus(btn_lnch);
    /* Start 1-second countdown timer */
    if (timeout_s > 0 && slot_idx >= 0) {
        s_timer = lv_timer_create(countdown_timer_cb, 1000, NULL);
    }

    ESP_LOGI(TAG, "boot screen: slot=%d timeout=%ds", slot_idx, timeout_s);
}
