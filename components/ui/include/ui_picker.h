#pragma once
#include "ui_form.h"
/* Folders plus PNG/JPEG; filesystem work runs outside LVGL. */
void ui_picker_open(const char *title,bool wallpaper,ui_form_save_fn save,void *user);
void ui_picker_close(void);
