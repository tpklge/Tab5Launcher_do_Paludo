#include <assert.h>
#include <stdio.h>
void ui_style_refresh(void) {}
#include "ui_nav.h"
#include "../../components/ui/ui_progress.c"
static int clicks,cancels;
static void clicked(lv_event_t *e) { clicks++; }
static void cancel(void *arg) { cancels++; }
static void close_modal(lv_event_t *e) { lv_obj_delete(lv_event_get_target(e)); }
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) { lv_display_flush_ready(d); }
int main(void) {
 lv_init(); lv_display_t *d=lv_display_create(1280,720);
 static uint8_t pixels[1280*20*4]; lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
 lv_obj_t *root=lv_screen_active(); ui_nav_init(root);
 lv_obj_t *a=lv_button_create(root),*b=lv_button_create(root),*check=lv_checkbox_create(root);
 lv_obj_add_event_cb(a,clicked,LV_EVENT_CLICKED,NULL); lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,NULL);
 lv_obj_add_state(b,LV_STATE_DISABLED); ui_nav_refresh(); lv_obj_clear_state(b,LV_STATE_DISABLED); ui_nav_focus(a); ui_nav_send_key(LV_KEY_ENTER); assert(clicks==1);
 ui_nav_send_key(LV_KEY_DOWN); assert(ui_nav_focused()==b);
 ui_nav_send_key(LV_KEY_ENTER); assert(clicks==2);
 ui_nav_send_key(LV_KEY_NEXT); assert(ui_nav_focused()==check);
 ui_nav_send_key(LV_KEY_ENTER); assert(lv_obj_has_state(check,LV_STATE_CHECKED));
 ui_nav_focus(b);
 for(int i=0;i<100;i++) {
  lv_obj_t *modal=lv_obj_create(lv_layer_top()); ui_nav_modal(modal);
  lv_obj_add_event_cb(modal,close_modal,LV_EVENT_CANCEL,NULL);
  lv_obj_t *inside=lv_button_create(modal); ui_nav_refresh(); ui_nav_focus(inside);
  ui_nav_send_key(LV_KEY_NEXT); assert(ui_nav_focused()==inside);
  ui_nav_send_key(LV_KEY_ESC); assert(!ui_nav_has_modal() && ui_nav_focused()==b);
  ui_progress_open("INSTALL",cancel,NULL); assert(ui_progress_visible() && ui_nav_has_modal());
  ui_progress_update(70,"writing"); assert(lv_bar_get_value(s_bar)==70);
  ui_progress_update(20,"backup"); assert(lv_bar_get_value(s_bar)==70);
  int before=cancels; ui_nav_send_key(LV_KEY_ESC); assert(cancels==before+1);
  ui_progress_set_cancelable(false); ui_nav_send_key(LV_KEY_ESC); assert(cancels==before+1);
  ui_progress_update(100,"verified"); assert(lv_bar_get_value(s_bar)==100);
  ui_progress_close(); assert(!ui_nav_has_modal() && ui_nav_focused()==b);
 }
 lv_deinit(); puts("Navigation: native Enter clicks, arrows/Tab, checkbox, modal isolation/Escape, progress/cancel and 100 lifecycles passed (ASan/UBSan).");
}
