#pragma once
/* boot_mgr — boot-screen countdown, app launch, return-to-launcher, boot-loop guard.
 *
 * The launcher (factory app) boots by default when otadata is erased.
 * Installed apps live in ota_0..ota_N slots; the user picks a default via boot_mgr_set_default().
 * The custom launcher_policy bootloader defaults to factory and consumes one
 * durable request before selecting an OTA app once. RESET then returns to factory.
 * SD state tracks launches and consecutive panic/watchdog returns across unrelated
 * app RAM layouts. Any return suppresses autoboot for that boot; three failures
 * stop the slot until explicitly reactivated. No cross-reset uptime is inferred.
 */
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOOT_MGR_GUARD_MAX_RESTARTS  3
#define BOOT_MGR_GUARD_WINDOW_MS     15000  /* 15 s */
#define BOOT_MGR_DEFAULT_TIMEOUT_S   3      /* countdown seconds */
#define BOOT_MGR_MAX_SLOTS           16

/* State saved by the guard on behalf of an app that tripped the threshold */
typedef struct {
    int slot;      /* ota index of the stopped app */
    int restarts;  /* how many rapid restarts were detected */
    int returns;   /* how many early returns to the launcher */
} boot_mgr_guard_state_t;

/* Call at the very start of app_main: reads and clears the guard registers.
 * Returns true when an app was stopped; *state describes it. */
bool boot_mgr_guard_take(boot_mgr_guard_state_t *state);

/* Mark a slot as "stopped by guard"; the UI shows a warning and skips autoboot. */
void boot_mgr_mark_stopped(int slot);

/* Clear the stopped flag (user chose to boot it again, or it was erased). */
void boot_mgr_clear_stopped(void);

/* Index of the stopped slot, -1 if none. */
int boot_mgr_stopped_slot(void);

/* ---- Configuration -------------------------------------------------------- */

/* Set the default app slot (-1 to disable autoboot). Persists to SD. */
esp_err_t boot_mgr_set_default(int slot);
int       boot_mgr_get_default(void);           /* -1 = none */

/* Countdown timeout in seconds (1–10). */
esp_err_t boot_mgr_set_timeout(uint8_t seconds);
uint8_t   boot_mgr_get_timeout(void);

/* ---- Boot screen ---------------------------------------------------------- */

/* Show the boot screen and handle countdown + user input.
 * Returns when the user chose to stay in the launcher (pressed LAUNCHER button,
 * or no default app is configured, or the app is stopped by guard).
 * If the user (or timer) selects an app, launches it and does not return. */
void boot_mgr_start(void);
/* -1 after returning from any app, stopped app or configured deep-sleep wake. */
int boot_mgr_autoboot_slot(void);

/* ---- Launch -------------------------------------------------------------- */

/* Shut down peripherals and restart into the given OTA slot.
 * Verifies the image before switching otadata; returns ESP_FAIL if invalid.
 * Does not return on success. */
esp_err_t boot_mgr_launch_slot(int slot);
/* Capability emitted by the custom bootloader in the request partition. */
bool boot_mgr_return_supported(void);
typedef void (*boot_mgr_launch_cb_t)(esp_err_t result,void *user);
esp_err_t boot_mgr_launch_async(int slot,boot_mgr_launch_cb_t callback,void *user);

/* Select the verified factory launcher and restart; returns on selection failure. */
void boot_mgr_return_to_launcher(void);

#ifdef __cplusplus
}
#endif

/* Worker-only handoff preflight, registered by application integration. */
void boot_mgr_set_prepare(esp_err_t (*prepare)(int));

void boot_mgr_set_cancel_prepare(void (*cancel)(void));


/* Required policy for manual as well as automatic launches. */
void boot_mgr_set_recovery_check(bool (*required)(void));
