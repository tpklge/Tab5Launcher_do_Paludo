#pragma once
#include "lvgl.h"
void ui_assets_init(void);
/* Decodes in a worker. Missing/invalid/oversized files leave the current image. */
bool ui_assets_set(lv_obj_t *image,const char *path,unsigned max_width,unsigned max_height,unsigned box_width,unsigned box_height);

bool ui_assets_set_blur(lv_obj_t *,const char *,unsigned,unsigned,unsigned,unsigned);
