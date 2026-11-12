#pragma once
#include "installer.h"
esp_err_t backup_init(void);
esp_err_t backup_slot(int slot,installer_progress_fn progress,void *user,char *message,size_t size);
