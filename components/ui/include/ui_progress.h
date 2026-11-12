#pragma once
#include "lvgl.h"
typedef void (*ui_progress_cancel_fn)(void *);
void ui_progress_open(const char *title,ui_progress_cancel_fn cancel,void *user);
void ui_progress_update(unsigned percent,const char *stage);
void ui_progress_close(void);
void ui_progress_set_cancelable(bool enabled);
bool ui_progress_visible(void);
