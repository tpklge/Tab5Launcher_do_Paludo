#pragma once
/* time_mgr — RTC RX8130CE sync, NTP, timezone, manual time entry.
 *
 * The RTC always stores UTC.  Timezone conversion happens only at display time.
 * Default timezone: America/Cuiaba (UTC-4, no DST since Brazil abolished it in 2019).
 * NTP sync is optional and async; boot does not wait for it.
 */
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIME_SOURCE_NONE = 0,  /* unknown / not set */
    TIME_SOURCE_RTC,       /* loaded from RX8130CE at boot */
    TIME_SOURCE_MANUAL,    /* set by user */
    TIME_SOURCE_NTP,       /* synchronized via NTP */
    TIME_SOURCE_PI,        /* synchronized from Raspberry Pi helper */
} time_source_t;

/* Initialize: reads RTC, sets system clock, applies timezone from storage config.
 * Does not start NTP.  Call from app_main after storage_init(). */
void time_init(void);

/* Start NTP sync task (async).  Has no effect if Wi-Fi is not connected. */
void time_start_ntp(void);

/* Sync NTP now (blocks up to timeout_s seconds).  Call from worker task. */
esp_err_t time_ntp_sync(unsigned timeout_s);

/* Set the system time and RTC manually.  utc_time must be a valid Unix timestamp. */
esp_err_t time_set_manual(time_t utc_time);

/* Apply a POSIX timezone string (e.g. "<-04>4").  Calls setenv + tzset. */
void time_apply_timezone(const char *posix_tz);

/* Current time source. */
time_source_t time_get_source(void);

/* true if the current time is considered valid (RTC was not invalid at boot and
 * time has been set by at least one source). */
bool time_is_valid(void);

/* Formatted local time for the status bar: "HH:MM" */
void time_fmt_hhmm(char *out, size_t len);

/* Formatted local date+time for display */
void time_fmt_full(char *out, size_t len);

#ifdef __cplusplus
}
#endif

/* Portuguese weekday and long date on two lines, in local time. */
void time_date_pt(const struct tm *value, char *out, size_t len);
void time_fmt_rest(char *out, size_t len);
