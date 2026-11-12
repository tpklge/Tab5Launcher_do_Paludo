#pragma once
#include <stdbool.h>
#include "esp_err.h"
bool board_sd_ready(void);
esp_err_t board_sd_mount(void);
#include "lvgl.h"
bool board_charger_enabled(void);
esp_err_t board_charger_set(bool);
esp_err_t board_power_off(void);
void board_backlight(uint8_t);
lv_display_t *board_display(void);

typedef enum { BOARD_CHARGE_UNKNOWN=-1,BOARD_CHARGE_ON_BATTERY,BOARD_CHARGE_CHARGING,BOARD_CHARGE_IDLE,BOARD_CHARGE_EXTERNAL } board_charge_t;
typedef struct { bool valid; int millivolts,milliamps,percent; board_charge_t charge; uint64_t sd_total,sd_free; } board_power_status_t;
void board_power_status(board_power_status_t *out);
