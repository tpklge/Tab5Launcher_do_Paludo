#include "board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void load_calibration(void);
#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "power_model.h"
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static board_power_status_t s_status;
static i2c_master_dev_handle_t s_ina;
static int64_t s_last;
static bool read_register(uint8_t reg, uint16_t *value) {
  uint8_t bytes[2];
  if (i2c_master_transmit_receive(s_ina, &reg, 1, bytes, 2, 100) != ESP_OK)
    return false;
  *value = ((uint16_t)bytes[0] << 8) | bytes[1];
  return true;
}
static bool probe(void) {
  i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                             .device_address = 0x41,
                             .scl_speed_hz = 100000};
  if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_ina) != ESP_OK)
    return false;
  uint16_t manufacturer = 0, device = 0;
  if (!read_register(0xFE, &manufacturer) || !read_register(0xFF, &device) ||
      manufacturer != 0x5449 || device != 0x2260)
    return false;
  uint8_t setup[] = {
      0x00, 0x45,
      0x27}; /* continuous bus/shunt conversion, 16-sample averaging */
  return i2c_master_transmit(s_ina, setup, sizeof(setup), 100) == ESP_OK;
}
static void power_worker(void *arg) {
  bool ready = false, reported = false;
  while (1) {
    if (!ready)
      ready = probe();
    uint16_t voltage = 0, shunt = 0;
    board_power_status_t next = {.percent = -1};
    if (ready && read_register(0x02, &voltage) && read_register(0x01, &shunt)) {
      power_sample_t sample;
      power_decode(voltage, (int16_t)shunt, &sample);
      power_calibration_t calibration;
      board_power_calibration_get(&calibration);
      power_apply_calibration(&sample, &calibration);
      next.valid = sample.valid;
      next.millivolts = sample.millivolts;
      next.milliamps = sample.milliamps;
      next.percent = sample.percent;
      next.charge = sample.flow < 0   ? BOARD_CHARGE_CHARGING
                    : sample.flow > 0 ? BOARD_CHARGE_ON_BATTERY
                                      : BOARD_CHARGE_IDLE;
      if (!reported) {
        ESP_LOGI("power", "INA226: %d mV, %d mA, estimate %d%%",
                 next.millivolts, next.milliamps, next.percent);
        reported = true;
      }
    } else {
      ready = false;
      reported = false;
      if (s_ina) {
        i2c_master_bus_rm_device(s_ina);
        s_ina = NULL;
      }
    }
    next.sd_total = board_sd_total_bytes();
    next.sd_free = board_sd_free_bytes();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status = next;
    s_last = esp_timer_get_time();
    xSemaphoreGive(s_lock);
    vTaskDelay(pdMS_TO_TICKS(ready ? 1000 : 5000));
  }
}
esp_err_t board_power_init(void) {
  if (s_task)
    return ESP_OK;
  load_calibration();
  if (!s_lock)
    s_lock = xSemaphoreCreateMutex();
  if (!s_lock)
    return ESP_ERR_NO_MEM;
  return xTaskCreate(power_worker, "power_monitor", 4096, NULL, 2, &s_task) ==
                 pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}
void board_power_status(board_power_status_t *out) {
  *out = (board_power_status_t){.percent = -1, .charge = BOARD_CHARGE_UNKNOWN};
  if (!s_lock)
    return;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  *out = s_status;
  if (esp_timer_get_time() - s_last > 6000000) {
    out->valid = false;
    out->percent = -1;
    out->charge = BOARD_CHARGE_UNKNOWN;
  }
  xSemaphoreGive(s_lock);
}
#include "cJSON.h"
#include "launcher_store.h"
static power_calibration_t s_calibration = {.gain_ppm = 1000000};
void board_power_calibration_get(power_calibration_t *c) {
  if (s_lock)
    xSemaphoreTake(s_lock, portMAX_DELAY);
  *c = s_calibration;
  if (s_lock)
    xSemaphoreGive(s_lock);
}
esp_err_t board_power_calibration_set(const power_calibration_t *c) {
  if (!power_calibration_valid(c))
    return ESP_ERR_INVALID_ARG;
  cJSON *j = cJSON_CreateObject();
  cJSON_AddNumberToObject(j, "gainPpm", c->gain_ppm);
  cJSON_AddNumberToObject(j, "voltageOffsetMv", c->voltage_offset_mv);
  cJSON_AddNumberToObject(j, "currentOffsetMa", c->current_offset_ma);
  cJSON *points = cJSON_AddArrayToObject(j, "points");
  for (unsigned i = 0; i < c->count; i++) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "mv", c->points[i].millivolts);
    cJSON_AddNumberToObject(p, "percent", c->points[i].percent);
    cJSON_AddItemToArray(points, p);
  }
  char *text = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!text)
    return ESP_ERR_NO_MEM;
  esp_err_t e =
      launcher_file_write(LAUNCHER_ROOT "/power.json", text, strlen(text));
  free(text);
  if (!e) {
    if (s_lock)
      xSemaphoreTake(s_lock, portMAX_DELAY);
    s_calibration = *c;
    if (s_lock)
      xSemaphoreGive(s_lock);
  }
  return e;
}
static void load_calibration(void) {
  FILE *f = fopen(LAUNCHER_ROOT "/power.json", "rb");
  if (!f)
    return;
  char text[2048];
  size_t n = fread(text, 1, sizeof(text) - 1, f);
  bool ok = !ferror(f) && fgetc(f) == EOF;
  fclose(f);
  if (!ok)
    return;
  text[n] = 0;
  cJSON *j = cJSON_ParseWithOpts(text, NULL, true);
  power_calibration_t c = {0};
  cJSON *gain = cJSON_GetObjectItemCaseSensitive(j, "gainPpm"),
        *voltage = cJSON_GetObjectItemCaseSensitive(j, "voltageOffsetMv"),
        *current = cJSON_GetObjectItemCaseSensitive(j, "currentOffsetMa"),
        *points = cJSON_GetObjectItemCaseSensitive(j, "points");
  if (cJSON_IsNumber(gain) && cJSON_IsNumber(voltage) &&
      cJSON_IsNumber(current) && cJSON_IsArray(points)) {
    c.gain_ppm = gain->valueint;
    c.voltage_offset_mv = voltage->valueint;
    c.current_offset_ma = current->valueint;
    c.count = cJSON_GetArraySize(points);
    if (c.count <= POWER_CURVE_POINTS) {
      for (unsigned i = 0; i < c.count; i++) {
        cJSON *p = cJSON_GetArrayItem(points, i),
              *mv = cJSON_GetObjectItemCaseSensitive(p, "mv"),
              *pc = cJSON_GetObjectItemCaseSensitive(p, "percent");
        if (!cJSON_IsNumber(mv) || !cJSON_IsNumber(pc)) {
          c.count = POWER_CURVE_POINTS + 1;
          break;
        }
        c.points[i] = (power_curve_point_t){mv->valueint, pc->valueint};
      }
      if (power_calibration_valid(&c))
        s_calibration = c;
    }
  }
  cJSON_Delete(j);
}
