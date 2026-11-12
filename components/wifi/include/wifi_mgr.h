#pragma once
/* wifi_mgr — Wi-Fi via the ESP32-C6 coprocessor over ESP-Hosted 2.12.6 / SDIO.
 *
 * The ESP32-P4 has no native Wi-Fi.  The C6 is powered via PI4IOE and communicates
 * over SDIO using ESP-Hosted.  This component provides the standard esp_wifi /
 * esp_netif API surface to the rest of the launcher.
 *
 * All state changes are reported to a registered listener, called with the LVGL
 * lock held so the UI can update safely.
 *
 * Recovery actions (exposed to Settings > Connectivity):
 *   1. wifi_mgr_retry       — reattempt blocked connection
 *   2. wifi_mgr_reconnect   — disconnect + reconnect, keep saved networks
 *   3. wifi_mgr_restart_driver — deinit/reinit ESP-Hosted transport
 *   4. wifi_mgr_reset_c6    — power-cycle C6 and reinit ESP-Hosted
 *   5. wifi_mgr_forget_all  — separate, confirmed action
 *   6. wifi_mgr_diagnostics — return a diagnostic string
 *
 * Sources: UNIZERSION l5_wifi.h (da5aa984), ESP-Hosted 2.12.6 docs, esp_wifi_remote.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_SAVED   8
#define WIFI_MAX_SCAN   16

typedef enum {
    WIFI_STATE_DISABLED,       /* C6 not started */
    WIFI_STATE_INIT_C6,        /* powering C6, starting ESP-Hosted */
    WIFI_STATE_TRANSPORT,      /* ESP-Hosted transport up, initializing esp_wifi */
    WIFI_STATE_READY,          /* station mode active, idle */
    WIFI_STATE_SCANNING,
    WIFI_STATE_AUTHENTICATING, /* associating */
    WIFI_STATE_GETTING_IP,     /* DHCP */
    WIFI_STATE_CONNECTED,
    WIFI_STATE_RECONNECTING,
    WIFI_STATE_RECOVERING,
    WIFI_STATE_FAILED,         /* terminal failure, diagnostics available */
} wifi_state_t;

typedef struct {
    wifi_state_t state;
    bool         associated;   /* AUTHENTICATING: AP accepted us, waiting DHCP */
    char         ssid[33];     /* AUTHENTICATING / CONNECTED */
    char         ip[16];       /* CONNECTED */
    bool         network_saved; /* last successful credentials committed to NVS */
    uint16_t     disconnect_reason; /* actual driver reason, 0 until a failure */
    int          rssi;         /* CONNECTED, dBm */
    char         c6_fw[16];    /* ESP-Hosted version on C6 ("2.12.6"), "" until queried */
    char         error[160];   /* FAILED */
    bool         c6_mismatch;  /* FAILED: C6 runs wrong ESP-Hosted major.minor */
} wifi_status_t;

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    uint8_t auth;  /* wifi_auth_mode_t */
} wifi_ap_t;

typedef enum {
    WIFI_RESULT_OK,
    WIFI_RESULT_ERR_PASSWORD,  /* AP rejected the credential */
    WIFI_RESULT_ERR_NOT_FOUND, /* SSID not found in scan */
    WIFI_RESULT_ERR_REFUSED,   /* association refused */
    WIFI_RESULT_ERR_TIMEOUT,   /* no IP address in time */
    WIFI_RESULT_ERR_C6,        /* C6 not running or not responding */
    WIFI_RESULT_ERR_TRANSPORT, /* ESP-Hosted transport failure */
} wifi_result_t;

typedef void (*wifi_scan_cb_t)(esp_err_t err, const wifi_ap_t *aps, int n, void *user);
typedef void (*wifi_connect_cb_t)(wifi_result_t res, const char *detail, void *user);
typedef void (*wifi_listener_t)(void);  /* called with LVGL lock held */

/* Start Wi-Fi subsystem (async).  autoconnect: join the most recently successful saved network.
 * Does not block; call from app_main after ui_init. */
void wifi_init(bool autoconnect);

/* Pause before handing off to another app (stops Wi-Fi; SD and C6 use separate slots). */
void wifi_pause(void);
/* Worker acknowledgement; independent of asynchronous connection events. */
esp_err_t wifi_pause_wait(uint32_t timeout_ms);

/* Resume after a power cycle kept the hardware alive (USB connected). */
void wifi_resume(void);

/* Register state-change listener (called with LVGL lock). */
void wifi_set_listener(wifi_listener_t cb);

void wifi_get_status(wifi_status_t *out);

/* Async requests.  Return ESP_ERR_INVALID_STATE if another request is pending. */
esp_err_t wifi_scan(wifi_scan_cb_t cb, void *user);
esp_err_t wifi_connect(const char *ssid, const char *pass, uint8_t auth,
                       wifi_connect_cb_t cb, void *user);
esp_err_t wifi_disconnect(void);
esp_err_t wifi_refresh_signal(void);  /* update RSSI for connected AP */

/* Saved networks: call once from app_main before creating the UI/worker. */
esp_err_t wifi_saved_init(void);
int       wifi_saved_count(void);
int       wifi_saved_ssids(char (*ssids)[33], int max);
bool      wifi_saved_password(const char *ssid, char *pass, size_t len);
void      wifi_forget(const char *ssid);
void      wifi_forget_all(void);

/* Recovery actions */
void      wifi_retry(void);
void      wifi_reconnect(void);
esp_err_t wifi_restart_driver(void);
esp_err_t wifi_reset_c6(void);
const char *wifi_diagnostics(void);  /* static buffer, single caller at a time */

/* Helpers */
const char *wifi_auth_name(uint8_t auth);
bool        wifi_auth_needs_password(uint8_t auth);
bool        wifi_auth_supported(uint8_t auth);
bool        wifi_password_too_short(uint8_t auth, const char *pass);
const char *wifi_host_version(void);   /* "2.12.6" */
const char *wifi_host_series(void);    /* "2.12" — required C6 firmware series */

#ifdef __cplusplus
}
#endif
