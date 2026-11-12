/* Real LVGL keypad events generate press/release/click, unlike group_send_data alone. */
#include "ui_nav.h"
extern void ui_style_refresh(void) __attribute__((weak));
static lv_group_t *s_main,*s_modal_group;
static lv_indev_t *s_keypad;
static lv_obj_t *s_root,*s_modal,*s_restore;
static uint32_t s_key;
static bool s_pending,s_pressed;
static lv_group_t *active(void) { return s_modal_group?s_modal_group:s_main; }
static bool focusable(lv_obj_t *o)
{
    return lv_obj_check_type(o,&lv_button_class) || lv_obj_check_type(o,&lv_checkbox_class) ||
        lv_obj_check_type(o,&lv_dropdown_class) || lv_obj_check_type(o,&lv_slider_class) ||
        lv_obj_check_type(o,&lv_switch_class) || lv_obj_check_type(o,&lv_textarea_class);
}
static void focused(lv_event_t *event)
{
    lv_obj_t *object=lv_event_get_target(event);
    lv_obj_scroll_to_view_recursive(object,LV_ANIM_OFF);
}
static void collect(lv_obj_t *root,lv_group_t *group)
{
    if (!root || lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN)) return;
    if (focusable(root)) { /* LVGL skips disabled controls until they become enabled. */
        lv_group_add_obj(group,root);
        lv_obj_set_style_outline_color(root,lv_color_hex(0xFF4254),LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(root,3,LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(root,2,LV_STATE_FOCUS_KEY);
        lv_obj_remove_event_cb(root,focused);
        lv_obj_add_event_cb(root,focused,LV_EVENT_FOCUSED,NULL);
    }
    for (unsigned i=0;i<lv_obj_get_child_count(root);i++) collect(lv_obj_get_child(root,i),group);
}
static void read_key(lv_indev_t *indev,lv_indev_data_t *data)
{
    data->key=s_key; data->continue_reading=false;
    if (s_pressed) { s_pressed=false; data->state=LV_INDEV_STATE_RELEASED; }
    else if (s_pending) { s_pending=false; s_pressed=true; data->state=LV_INDEV_STATE_PRESSED; data->continue_reading=true; }
    else data->state=LV_INDEV_STATE_RELEASED;
}
void ui_nav_init(lv_obj_t *root)
{
    s_root=root;
    s_main=lv_group_create(); lv_group_set_default(s_main);
    s_keypad=lv_indev_create(); lv_indev_set_type(s_keypad,LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keypad,read_key); lv_indev_set_group(s_keypad,s_main);
}
void ui_nav_refresh(void)
{
    if(ui_style_refresh) ui_style_refresh();
    if (!s_main) return; /* standalone host views may omit the application root */
    lv_group_t *group=active();
    lv_obj_t *previous=lv_group_get_focused(group);
    lv_group_remove_all_objs(group);
    collect(s_modal?s_modal:s_root,group);
    if (previous && lv_obj_is_valid(previous) && lv_obj_get_group(previous)==group) lv_group_focus_obj(previous);
}
static void modal_deleted(lv_event_t *event)
{
    if (lv_event_get_target(event)!=s_modal) return;
    s_modal=NULL;
    lv_group_t *group=s_modal_group; s_modal_group=NULL;
    if (group) lv_group_delete(group);
    lv_group_set_default(s_main);
    if (s_keypad) lv_indev_set_group(s_keypad,s_main);
    ui_nav_refresh();
    if (s_restore && lv_obj_is_valid(s_restore) && lv_obj_get_group(s_restore)==s_main) lv_group_focus_obj(s_restore);
    s_restore=NULL;
}
void ui_nav_modal(lv_obj_t *root)
{
    if (!s_main || s_modal) return;
    s_restore=lv_group_get_focused(s_main); s_modal=root;
    s_modal_group=lv_group_create(); lv_group_set_default(s_modal_group);
    lv_indev_set_group(s_keypad,s_modal_group);
    lv_obj_add_event_cb(root,modal_deleted,LV_EVENT_DELETE,NULL);
}
bool ui_nav_has_modal(void) { return s_modal!=NULL; }
lv_obj_t *ui_nav_focused(void) { return active()?lv_group_get_focused(active()):NULL; }
void ui_nav_focus(lv_obj_t *object)
{
    if (active() && object && lv_obj_is_valid(object) && lv_obj_get_group(object)==active()) {
        lv_group_set_editing(active(),false); lv_group_focus_obj(object);
        lv_obj_add_state(object,LV_STATE_FOCUS_KEY);
    }
}
static lv_obj_t *first(lv_obj_t *root)
{
    if (lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (focusable(root) && !lv_obj_has_state(root,LV_STATE_DISABLED)) return root;
    for (unsigned i=0;i<lv_obj_get_child_count(root);i++) { lv_obj_t *o=first(lv_obj_get_child(root,i)); if (o) return o; }
    return NULL;
}
void ui_nav_focus_first(lv_obj_t *root) { if (root) ui_nav_focus(first(root)); }
void ui_nav_send_key(uint32_t key)
{
    if (!s_keypad) return;
    if (key==LV_KEY_ESC && s_modal) {
        /* Modal owners can implement Escape without activating a destructive button. */
        lv_obj_send_event(s_modal,LV_EVENT_CANCEL,NULL); return;
    }
    lv_obj_t *object=ui_nav_focused();
    bool editing=active() && lv_group_get_editing(active());
    bool textarea=object && lv_obj_check_type(object,&lv_textarea_class);
    bool dropdown=object && lv_obj_check_type(object,&lv_dropdown_class) && lv_dropdown_is_open(object);
    if (!editing && !textarea && !dropdown) {
        if (key==LV_KEY_UP) key=LV_KEY_PREV;
        if (key==LV_KEY_DOWN) key=LV_KEY_NEXT;
        if (object && lv_obj_check_type(object,&lv_button_class)) {
            if (key==LV_KEY_LEFT) key=LV_KEY_PREV;
            if (key==LV_KEY_RIGHT) key=LV_KEY_NEXT;
        }
    }
    s_key=key; s_pending=true;
    lv_indev_read(s_keypad); /* consumes a genuine press and release in this LVGL turn */
}
