/* RX8130CE UTC calendar; backup charging stays disabled. */
#include "board.h"
#include "bsp/m5stack_tab5.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "rtc_codec.h"
#include <stdatomic.h>
#include <sys/time.h>
static i2c_master_dev_handle_t s_rtc;
static SemaphoreHandle_t s_lock;
static atomic_bool s_valid;
static esp_err_t read_reg(uint8_t reg, uint8_t *out, size_t n) {
  return i2c_master_transmit_receive(s_rtc, &reg, 1, out, n, 100);
}
static esp_err_t write_reg(uint8_t reg, uint8_t v) {
  uint8_t b[] = {reg, v};
  return i2c_master_transmit(s_rtc, b, 2, 100);
}
esp_err_t board_rtc_init(void) {
  if (!s_lock)
    s_lock = xSemaphoreCreateMutex();
  if (!s_lock)
    return ESP_ERR_NO_MEM;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  esp_err_t err = ESP_OK;
  if (!s_rtc) {
    i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                             .device_address = 0x32,
                             .scl_speed_hz = 100000};
    err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &c, &s_rtc);
  }
  uint8_t flags = 2, ctrl = 0, data[7];
  time_t utc;
  if (err == ESP_OK)
    err = read_reg(0x1f, &ctrl, 1);
  if (err == ESP_OK)
    err = write_reg(0x1f,
                    (ctrl & ~0x20) |
                        0x10); /* INIEN; never charge a primary backup cell. */
  if (err == ESP_OK)
    err = write_reg(0x1c, 0); /* no inherited periodic timer */
  if (err == ESP_OK)
    err = write_reg(0x1e, 0); /* run calendar, interrupts/test off */
  if (err == ESP_OK)
    err = write_reg(0x30, 0); /* digital correction disabled */
  if (err == ESP_OK)
    err = read_reg(0x1d, &flags, 1);
  if (err == ESP_OK && !(flags & 2) && read_reg(0x10, data, 7) == ESP_OK &&
      rtc_decode(data, &utc)) {
    struct timeval tv = {.tv_sec = utc};
    if (settimeofday(&tv, NULL) == 0)
      s_valid = true;
  }
  xSemaphoreGive(s_lock);
  return err;
}
bool board_rtc_valid(void) { return s_valid; }
esp_err_t board_rtc_set(time_t utc) {
  uint8_t data[8] = {0x10};
  if (!rtc_encode(utc, data + 1))
    return ESP_ERR_INVALID_ARG;
  if (!s_rtc || !s_lock)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  /* STOP calendar, write coherent fields, resume even when the write fails. */
  uint8_t ctrl = 0;
  esp_err_t err = read_reg(0x1e, &ctrl, 1);
  if (err == ESP_OK)
    err = write_reg(0x1e, (ctrl & ~0x80) | 0x40);
  if (err == ESP_OK)
    err = i2c_master_transmit(s_rtc, data, sizeof(data), 100);
  esp_err_t resume = write_reg(0x1e, ctrl & ~0xc0);
  if (err == ESP_OK)
    err = resume;
  if (err == ESP_OK)
    err = write_reg(0x1d, 0xbd); /* W0C: clear VLF only, retain other flags. */
  if (err == ESP_OK) {
    uint8_t flags = 2;
    err = read_reg(0x1d, &flags, 1);
    if (flags & 2)
      err = ESP_ERR_INVALID_STATE;
  }
  s_valid = err == ESP_OK;
  xSemaphoreGive(s_lock);
  return err;
}
