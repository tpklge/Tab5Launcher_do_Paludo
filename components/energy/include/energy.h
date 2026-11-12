#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t energy_init(bool (*idle)(void),void (*notice)(const char*));
