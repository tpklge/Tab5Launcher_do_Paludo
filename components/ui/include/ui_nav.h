#pragma once
#include "lvgl.h"
void ui_nav_init(lv_obj_t *root);
void ui_nav_refresh(void);
void ui_nav_modal(lv_obj_t *root);
bool ui_nav_has_modal(void);
lv_obj_t *ui_nav_focused(void);
void ui_nav_focus(lv_obj_t *object);
void ui_nav_focus_first(lv_obj_t *root);
void ui_nav_send_key(uint32_t key);
