/* One-shot app launch. Every boot without a consumed, valid request selects
 * factory. Uses the SDK image loader/verifier; does not patch managed SDK
 * components. */
#include "boot_request.h"
#include "bootloader_flash_priv.h"
#include "bootloader_utility.h"
#include "esp_log.h"
#include <string.h>
void bootloader_hooks_include(void) {}
/* Restore an interrupted table write before the SDK reads the table. */
static bool recover_layout(void) {
  layout_recovery_t tx;
  static uint8_t table[4096];
  if (bootloader_flash_read(LAYOUT_TX_OFFSET, &tx, sizeof(tx), false) !=
          ESP_OK ||
      !layout_recovery_valid(&tx))
    return true;
  if (bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET, table, sizeof(table),
                            false) != ESP_OK)
    return false;
  uint32_t current = boot_crc_bytes(table, sizeof(table));
  if (current != tx.new_crc && current != tx.old_crc) {
    if (bootloader_flash_read(LAYOUT_MIRROR_OFFSET, table, sizeof(table),
                              false) != ESP_OK ||
        boot_crc_bytes(table, sizeof(table)) != tx.old_crc)
      return false;
    if (bootloader_flash_erase_range(ESP_PARTITION_TABLE_OFFSET,
                                     sizeof(table)) != ESP_OK ||
        bootloader_flash_write(ESP_PARTITION_TABLE_OFFSET, table, sizeof(table),
                               false) != ESP_OK)
      return false;
    if (bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET, table, sizeof(table),
                              false) != ESP_OK ||
        boot_crc_bytes(table, sizeof(table)) != tx.old_crc)
      return false;
  }
  uint32_t zero = 0;
  return bootloader_flash_write(LAYOUT_TX_OFFSET, &zero, sizeof(zero), false) ==
         ESP_OK;
}
void bootloader_after_init(void) {
  if (!recover_layout()) {
    ESP_LOGE("launcher_policy",
             "layout recovery incomplete; SDK recovery required");
    return;
  }
  bootloader_state_t state = {0};
  if (!bootloader_utility_load_partition_table(&state) || !state.factory.size)
    return;
  bool have_request_partition = false;
  for (unsigned i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; i++) {
    esp_partition_info_t p;
    if (bootloader_flash_read(ESP_PARTITION_TABLE_OFFSET + i * sizeof(p), &p,
                              sizeof(p), true) != ESP_OK)
      break;
    if (p.magic != ESP_PARTITION_MAGIC)
      break;
    if (p.type == PART_TYPE_DATA && p.subtype == 0x40 &&
        p.pos.offset == BOOT_REQUEST_OFFSET &&
        p.pos.size == BOOT_REQUEST_SIZE &&
        memcmp(p.label, "launch_req\0", 11) == 0)
      have_request_partition = true;
  }
  int selected = FACTORY_INDEX;
  if (have_request_partition) {
    boot_request_t request;
    if (bootloader_flash_read(BOOT_REQUEST_OFFSET, &request, sizeof(request),
                              false) == ESP_OK &&
        boot_request_valid(&request) &&
        state.ota[request.slot].offset == request.address &&
        state.ota[request.slot].size == request.size) {
      uint32_t consumed = 0;
      if (bootloader_flash_write(BOOT_REQUEST_OFFSET, &consumed,
                                 sizeof(consumed), false) == ESP_OK &&
          bootloader_flash_erase_range(BOOT_REQUEST_OFFSET,
                                       BOOT_REQUEST_SIZE) == ESP_OK)
        selected = (int)request.slot; /* invalidate before erase/handoff */
    }
    uint32_t capability = 0;
    if (bootloader_flash_read(BOOT_REQUEST_OFFSET + BOOT_CAP_OFFSET,
                              &capability, sizeof(capability),
                              false) == ESP_OK &&
        capability == 0xFFFFFFFFu) {
      capability = BOOT_CAP_MAGIC;
      bootloader_flash_write(BOOT_REQUEST_OFFSET + BOOT_CAP_OFFSET, &capability,
                             sizeof(capability), false);
    }
  }
  if (have_request_partition) {
    uint32_t cap = 0;
    if (bootloader_flash_read(BOOT_REQUEST_OFFSET + LAYOUT_CAP_OFFSET, &cap,
                              sizeof(cap), false) == ESP_OK &&
        cap == 0xffffffffu) {
      cap = LAYOUT_CAP_MAGIC;
      bootloader_flash_write(BOOT_REQUEST_OFFSET + LAYOUT_CAP_OFFSET, &cap,
                             sizeof(cap), false);
    }
  }
  ESP_LOGI("launcher_policy",
           "one-shot boot index %d (next reset returns to launcher)", selected);
  bootloader_utility_load_boot_image(&state, selected);
}
