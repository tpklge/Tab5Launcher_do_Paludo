#pragma once
/* board — hardware init and peripheral access for the M5Stack Tab5.
 *
 * Hardware: ESP32-P4 + display MIPI-DSI 1280×720 (ILI9881C+GT911 or ST7123+ST712x),
 * backlight, SD card (SDMMC), RTC RX8130CE (I²C BSP bus), INA226 battery monitor,
 * PI4IOE GPIO expander (charger, C6 power, USB 5 V, power-off).
 *
 * Sources consulted: UNIZERSION l5_board.h (da5aa984), M5Stack Tab5 BSP 1.3.2 schematic.
 */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Hardware revision -------------------------------------------------- */

typedef enum {
    BOARD_REV_UNKNOWN = 0,
    BOARD_REV_V1_ILI9881C,   /* first production run: ILI9881C + GT911 */
    BOARD_REV_V2_ST7123,     /* later units: ST7123 + ST712x */
} board_rev_t;

board_rev_t board_get_rev(void);
const char *board_rev_name(board_rev_t rev);  /* "ILI9881C+GT911" / "ST7123+ST712x" */

/* ---- Init / deinit ------------------------------------------------------- */

/* Full hardware init: charger, display (MIPI-DSI + touch), SD, RTC, battery.
 * Must be called once from app_main before any other board_* call.
 * Returns ESP_OK or the first error encountered.  Non-fatal peripherals
 * (RTC, battery) log a warning and continue. */
esp_err_t board_init(void);

/* Ordered shutdown before handing off to another app or rebooting.
 * Stops Wi-Fi (via wifi_mgr), flushes SD, deinits display DMA, etc.
 * Safe to call even if board_init partially failed. */
void board_shutdown(void);

/* ---- Display / LVGL ------------------------------------------------------ */

/* Returns the LVGL display handle; NULL if display did not start. */
lv_display_t *board_display(void);

/* Acquire/release the LVGL mutex (wraps esp_lvgl_port_lock/unlock).
 * timeout_ms = 0 → wait forever. */
bool board_lvgl_lock(uint32_t timeout_ms);
void board_lvgl_unlock(void);

/* Backlight 0–100 % (0 = off). */
void board_backlight(uint8_t percent);

/* Show the first completed frame, then set backlight.  Call once after ui_init. */
void board_reveal(uint8_t percent);

/* true while a finger is on the screen (call with LVGL lock held) */
bool board_touch_pressed(void);

/* Rotation: 0 = landscape (default), 180 = landscape-inverted */
void board_set_rotation(uint16_t degrees);

/* ---- SD card ------------------------------------------------------------- */

typedef enum {
    BOARD_SD_UNCHECKED,
    BOARD_SD_MOUNTED,
    BOARD_SD_NO_CARD,
    BOARD_SD_UNREADABLE,
    BOARD_SD_ERROR,
} board_sd_state_t;

/* Mount /sdcard.  Never formats.  Safe to call again after failure. */
esp_err_t board_sd_mount(void);
bool board_sd_ready(void);
board_sd_state_t board_sd_state(void);
uint64_t board_sd_total_bytes(void);
uint64_t board_sd_free_bytes(void);
const char *board_sd_status(bool detail);  /* short tag or full sentence */

/* ---- RTC (RX8130CE) ------------------------------------------------------ */

/* Set the RTC to a UTC timestamp.  The RTC always stores UTC. */
esp_err_t board_rtc_init(void);
esp_err_t board_rtc_set(time_t utc);

/* true if the RTC had valid time at boot (power was not lost) */
bool board_rtc_valid(void);

/* ---- Battery / power ----------------------------------------------------- */

/* Battery percentage 0–100, -1 when unknown. */
int board_battery_percent(void);

typedef enum {
    BOARD_CHARGE_UNKNOWN = -1,
    BOARD_CHARGE_ON_BATTERY,
    BOARD_CHARGE_CHARGING,
    BOARD_CHARGE_IDLE,      /* no significant current; USB presence is unknown */
    BOARD_CHARGE_EXTERNAL,   /* USB connected but charger not actively charging */
} board_charge_t;

typedef struct {
    bool valid;
    int millivolts,milliamps,percent;
    board_charge_t charge;
    uint64_t sd_total,sd_free;
} board_power_status_t;
esp_err_t board_power_init(void);
void board_power_status(board_power_status_t *out);
board_charge_t board_charge_state(void);

/* Enable the battery charger (PI4IOE CHG_EN, as Aurora Player).
 * Called at start-up and after recovery of a stuck I²C bus. */
esp_err_t board_charger_on(void);
esp_err_t board_charger_set(bool enable);
bool board_charger_enabled(void);

/* Hardware power-off via PI4IOE. Returns on failure. */
esp_err_t board_power_off(void);

/* ESP32-C6 (Wi-Fi) VCC rail (PI4IOE WLAN_PWR_EN). Off after power-on. */
esp_err_t board_c6_power(bool on);

/* USB-A 5 V rail (PI4IOE, Raspberry Pi supply). */
esp_err_t board_usb5v(bool on);
bool board_usb5v_on(void);

#ifdef __cplusplus
}
#endif

/* Maintenance boot only, before other SD clients. Transfers raw ownership. */
#include "sdmmc_cmd.h"
esp_err_t board_sd_export_card(sdmmc_card_t **);
#include "power_model.h"
void board_power_calibration_get(power_calibration_t *);
esp_err_t board_power_calibration_set(const power_calibration_t *);

/* Display/touch only; no SD, RTC or power-monitor worker. */
esp_err_t board_display_init(void);

/* Blank and retain backlight off across a controlled USB-mode restart. */
void board_display_guard_restart(void);
