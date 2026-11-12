#pragma once
#include "lvgl.h"
void ui_visual_init(lv_obj_t *root);
void ui_visual_reload(void);
/* Consumes the first key when waking the idle overlay or dark screen. */
bool ui_visual_activity(void);
