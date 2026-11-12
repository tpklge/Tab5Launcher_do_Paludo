#include "ui_progress.h"
#include "ui_nav.h"
static lv_obj_t *s_root,*s_bar,*s_stage,*s_percent,*s_cancel;
static ui_progress_cancel_fn s_callback;
static void *s_user;
static unsigned s_value;
static bool s_can_cancel;
static void cancelled(lv_event_t *event)
{
    if (!s_can_cancel) return;
    if (s_callback) s_callback(s_user);
    if (s_cancel) lv_obj_add_state(s_cancel,LV_STATE_DISABLED);
    if (s_stage) lv_label_set_text(s_stage,"Cancelamento solicitado. Aguarde um ponto seguro.");
}
static void deleted(lv_event_t *event)
{
    s_root=s_bar=s_stage=s_percent=s_cancel=NULL; s_callback=NULL; s_user=NULL;
}
void ui_progress_close(void) { if (s_root) lv_obj_delete(s_root); }
bool ui_progress_visible(void) { return s_root!=NULL; }
void ui_progress_open(const char *title,ui_progress_cancel_fn cancel,void *user)
{
    ui_progress_close(); s_value=0; s_can_cancel=cancel!=NULL; s_callback=cancel; s_user=user;
    s_root=lv_obj_create(lv_layer_top()); lv_obj_set_size(s_root,LV_PCT(100),LV_PCT(100));
    lv_obj_set_pos(s_root,0,0); lv_obj_set_style_bg_color(s_root,lv_color_hex(0x080A0F),0);
    lv_obj_set_style_bg_opa(s_root,LV_OPA_90,0); lv_obj_set_style_border_width(s_root,0,0);
    lv_obj_clear_flag(s_root,LV_OBJ_FLAG_SCROLLABLE); ui_nav_modal(s_root);
    lv_obj_add_event_cb(s_root,deleted,LV_EVENT_DELETE,NULL);
    lv_obj_add_event_cb(s_root,cancelled,LV_EVENT_CANCEL,NULL);
    lv_obj_t *panel=lv_obj_create(s_root); lv_obj_set_size(panel,760,350); lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x20242B),0); lv_obj_set_style_border_color(panel,lv_color_hex(0xEB2638),0);
    lv_obj_set_style_pad_all(panel,26,0); lv_obj_clear_flag(panel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *heading=lv_label_create(panel); lv_label_set_text(heading,title);
    lv_obj_set_style_text_font(heading,&lv_font_montserrat_24,0); lv_obj_set_style_text_color(heading,lv_color_hex(0xFFFFFF),0);
    s_stage=lv_label_create(panel); lv_label_set_text(s_stage,"Preparando..."); lv_obj_set_pos(s_stage,0,64);
    lv_obj_set_width(s_stage,690); lv_obj_set_style_text_color(s_stage,lv_color_hex(0xCBD5E1),0);
    s_bar=lv_bar_create(panel); lv_obj_set_pos(s_bar,0,122); lv_obj_set_size(s_bar,690,26); lv_bar_set_range(s_bar,0,100);
    lv_obj_set_style_bg_color(s_bar,lv_color_hex(0xEB2638),LV_PART_INDICATOR);
    s_percent=lv_label_create(panel); lv_label_set_text(s_percent,"0%"); lv_obj_set_pos(s_percent,0,168);
    lv_obj_set_style_text_color(s_percent,lv_color_hex(0xFFFFFF),0);
    s_cancel=lv_button_create(panel); lv_obj_set_size(s_cancel,180,46); lv_obj_align(s_cancel,LV_ALIGN_BOTTOM_RIGHT,0,0);
    lv_obj_t *label=lv_label_create(s_cancel); lv_label_set_text(label,"Cancelar / Esc"); lv_obj_center(label);
    lv_obj_add_event_cb(s_cancel,cancelled,LV_EVENT_CLICKED,NULL);
    if (!cancel) lv_obj_add_state(s_cancel,LV_STATE_DISABLED);
    ui_nav_refresh(); ui_nav_focus(s_cancel);
}
void ui_progress_update(unsigned percent,const char *stage)
{
    if (!s_root) return;
    if (percent>100) percent=100;
    if (percent<s_value) percent=s_value;
    s_value=percent;
    lv_bar_set_value(s_bar,(int)percent,LV_ANIM_OFF);
    lv_label_set_text_fmt(s_percent,"%u%%",percent);
    if (stage) lv_label_set_text(s_stage,stage);
}

void ui_progress_set_cancelable(bool enabled)
{
    s_can_cancel=enabled && s_callback;
    if (!s_cancel) return;
    if (s_can_cancel) lv_obj_clear_state(s_cancel,LV_STATE_DISABLED);
    else lv_obj_add_state(s_cancel,LV_STATE_DISABLED);
    ui_nav_refresh();
}
