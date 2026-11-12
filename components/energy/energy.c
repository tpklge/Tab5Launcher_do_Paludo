#include "energy.h"
#include "board.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "storage_guard.h"
#include "usb_storage.h"
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static bool (*safe_idle)(void);
static void (*show_notice)(const char *);
static bool started;
static void worker(void *unused) {
  unsigned low_samples = 0, critical_samples = 0;
  bool warned = false;
  int64_t last_log = 0;
  for (;;) {
    board_power_status_t p;
    board_power_status(&p);
    launcher_config_t cfg;
    storage_snapshot(&cfg);
    bool discharge = p.valid && p.charge == BOARD_CHARGE_ON_BATTERY;
    if (discharge && p.percent >= 0 && p.percent <= 15)
      low_samples++;
    else {
      low_samples = 0;
      if (!discharge || p.percent > 20)
        warned = false;
    }
    if (discharge && p.percent >= 0 && p.percent <= 3 && p.millivolts <= 6500)
      critical_samples++;
    else
      critical_samples = 0;
    if (cfg.battery_warning && low_samples >= 30 && !warned && safe_idle &&
        safe_idle()) {
      warned = true;
      if (show_notice)
        show_notice(
            "Bateria baixa. Conecte a alimentacao e salve seu trabalho.");
    }
    int64_t now = esp_timer_get_time();
    if (cfg.power_logging && p.valid && now - last_log >= 30000000 &&
        safe_idle && safe_idle() && storage_write_acquire()) {
      const char *path = LAUNCHER_DIAGNOSTICS "/power.csv";
      struct stat st;
      if (!stat(path, &st) && st.st_size >= 512 * 1024) {
        unlink(LAUNCHER_DIAGNOSTICS "/power.previous.csv");
        rename(path, LAUNCHER_DIAGNOSTICS "/power.previous.csv");
      }
      bool fresh = stat(path, &st) != 0;
      FILE *f = fopen(path, "a");
      if (f) {
        if (fresh)
          fputs("utc,uptime_s,millivolts,milliamps,estimate_percent,charge_"
                "state\n",
                f);
        fprintf(f, "%lld,%lld,%d,%d,%d,%d\n", (long long)time(NULL),
                (long long)(now / 1000000), p.millivolts, p.milliamps,
                p.percent, p.charge);
        fflush(f);
        fsync(fileno(f));
        fclose(f);
      }
      storage_write_release();
      last_log = now;
    }
    if (cfg.critical_shutdown && critical_samples >= 60 && safe_idle &&
        safe_idle()) {
      if (show_notice)
        show_notice("Bateria critica: desligamento em 10 segundos. Conecte a "
                    "alimentacao para cancelar.");
      bool cancel = false;
      for (unsigned i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        board_power_status(&p);
        storage_snapshot(&cfg);
        if (!cfg.critical_shutdown || !p.valid ||
            p.charge != BOARD_CHARGE_ON_BATTERY || p.millivolts > 6500 ||
            !safe_idle()) {
          cancel = true;
          break;
        }
      }
      if (!cancel && storage_exclusive_begin(1500)) {
        usb_storage_eject();
        for (unsigned i = 0; i < 50; i++) {
          usb_storage_status_t drive;
          usb_storage_status(&drive);
          if (!drive.mounted && !drive.ejecting)
            break;
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        usb_storage_status_t drive;
        usb_storage_status(&drive);
        if (!drive.mounted && !drive.ejecting && safe_idle()) {
          storage_save();
          board_power_off();
        }
        storage_exclusive_end();
      }
      critical_samples = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
esp_err_t energy_init(bool (*idle)(void), void (*notice)(const char *)) {
  if (started)
    return ESP_OK;
  safe_idle = idle;
  show_notice = notice;
  if (xTaskCreate(worker, "energy_policy", 6144, NULL, 2, NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  started = true;
  return ESP_OK;
}
