#pragma once
#include "lvgl.h"
#include <time.h>
typedef bool (*ui_form_save_fn)(const char *text,void *user);
/* false keeps the form open. Callback must copy text before returning. */
void ui_form_open(const char *title,const char *initial,bool multiline,unsigned max_length,ui_form_save_fn save,void *user);
void ui_form_close(void);
void ui_notice(const char *title,const char *message);

void ui_confirm(const char*,const char*,const char*,void (*)(void*),void*);
void ui_datetime_open(bool (*save)(time_t,void*),void *user);
lv_obj_t *ui_form_panel(void);
