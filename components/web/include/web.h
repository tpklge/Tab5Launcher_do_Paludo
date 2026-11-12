#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
esp_err_t web_init(void);
esp_err_t web_start(void);
void web_stop(void);
bool web_running(void);
/* Local display only. Never logged, never persisted. HTTP on trusted LAN. */
void web_access_token(char *out,size_t size);

void web_set_config_changed(void (*fn)(void));

bool web_io_busy(void);
