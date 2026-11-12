/* time_mgr.c */
#include "time_mgr.h"
#include "board.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "storage.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "time_mgr";

static atomic_int s_source = TIME_SOURCE_NONE;
static atomic_bool s_valid = false;

static void sntp_sync_cb(struct timeval *tv) {
  s_source = TIME_SOURCE_NTP;
  s_valid = true;
  /* Write back to RTC */
  board_rtc_set(tv->tv_sec);
  ESP_LOGI(TAG, "NTP sync OK: %ld", (long)tv->tv_sec);
}

void time_init(void) {
  const launcher_config_t *cfg = storage_get_config();

  /* Apply timezone */
  time_apply_timezone(cfg->timezone);

  /* RTC → system clock (done in board_init already; just check validity) */
  if (board_rtc_valid()) {
    s_source = TIME_SOURCE_RTC;
    s_valid = true;
    ESP_LOGI(TAG, "time from RTC");
  } else {
    ESP_LOGW(TAG, "RTC invalid — time unknown until NTP or manual set");
  }
}

void time_apply_timezone(const char *posix_tz) {
  if (!posix_tz || posix_tz[0] == 0)
    return;
  setenv("TZ", posix_tz, 1);
  tzset();
  ESP_LOGI(TAG, "timezone set: %s", posix_tz);
}

void time_start_ntp(void) {
  if (esp_sntp_enabled())
    return;
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.cloudflare.com");
  sntp_set_time_sync_notification_cb(sntp_sync_cb);
  esp_sntp_init();
  ESP_LOGI(TAG, "NTP started");
}

esp_err_t time_ntp_sync(unsigned timeout_s) {
  time_start_ntp();
  for (unsigned i = 0; i < timeout_s * 10; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
      return ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return ESP_ERR_TIMEOUT;
}

esp_err_t time_set_manual(time_t utc_time) {
  if (utc_time < 1000000000L)
    return ESP_ERR_INVALID_ARG; /* sanity: after ~2001 */
  struct timeval tv = {.tv_sec = utc_time, .tv_usec = 0};
  settimeofday(&tv, NULL);
  esp_err_t rtc_error = board_rtc_set(utc_time);
  s_source = TIME_SOURCE_MANUAL;
  s_valid = true;
  ESP_LOGI(TAG, "time set manually: %ld", (long)utc_time);
  return rtc_error;
}

time_source_t time_get_source(void) { return s_source; }
bool time_is_valid(void) { return s_valid; }

void time_fmt_hhmm(char *out, size_t len) {
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(out, len, "%H:%M", &tm);
}

void time_fmt_full(char *out, size_t len) {
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(out, len, "%d/%m/%Y %H:%M:%S", &tm);
}

void time_fmt_rest(char *out, size_t len) {
  time_t now = time(NULL);
  struct tm local;
  if (!localtime_r(&now, &local)) { time_date_pt(NULL, out, len); return; }
  time_date_pt(&local, out, len);
}
