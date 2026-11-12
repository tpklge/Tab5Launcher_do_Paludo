#pragma once
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_flash_internal.h"
/* Caller owns the installer lock and has verified the full snapshot, image
 * bounds, table digest and durable recovery guard. Never change sdkconfig's
 * global protection policy. Restore protection even after a failed write. */
static inline esp_err_t layout_table_publish(const uint8_t table[4096]) {
  if (!table)
    return ESP_ERR_INVALID_ARG;
  esp_err_t e =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  if (e)
    return e;
  e = esp_flash_erase_region(NULL, 0x8000, 4096);
  if (!e)
    e = esp_flash_write(NULL, table, 0x8000, 4096);
  esp_err_t restored =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  return e ? e : restored;
}
static inline esp_err_t layout_app_erase(uint32_t address, uint32_t size,
                                         uint32_t protected_end) {
  if (protected_end < 0x30000 || (protected_end & 65535) ||
      address < protected_end || address >= 0xa10000 ||
      size > 0xa10000 - address || !size || (address & 4095) || (size & 4095))
    return ESP_ERR_INVALID_ARG;
  esp_err_t e =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  if (e)
    return e;
  e = esp_flash_erase_region(NULL, address, size);
  esp_err_t restored =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  return e ? e : restored;
}
static inline esp_err_t layout_app_write(uint32_t address, const void *bytes,
                                         uint32_t size,
                                         uint32_t protected_end) {
  if (!bytes || protected_end < 0x30000 || (protected_end & 65535) ||
      address < protected_end || address >= 0xa10000 ||
      size > 0xa10000 - address || !size)
    return ESP_ERR_INVALID_ARG;
  esp_err_t e =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, false);
  if (e)
    return e;
  e = esp_flash_write(NULL, bytes, address, size);
  esp_err_t restored =
      esp_flash_set_dangerous_write_protection(esp_flash_default_chip, true);
  return e ? e : restored;
}
