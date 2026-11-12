#pragma once
#include "esp_partition.h"
#include <stdint.h>
/* The current allocator protects the tail. Describe its real partitions,
 * instead of presenting every protected/unallocated byte as app data. */
static inline uint32_t flash_layout_tail_allocated(void) {
  uint32_t used = 0;
  for (esp_partition_iterator_t it = esp_partition_find(
           ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
       it; it = esp_partition_next(it)) {
    const esp_partition_t *p = esp_partition_get(it);
    if (!p || p->address >= 0x1000000 || p->size > 0x1000000 - p->address)
      continue;
    uint32_t end = p->address + p->size;
    if (end > 0xa10000) {
      uint32_t start = p->address < 0xa10000 ? 0xa10000 : p->address;
      uint32_t bytes = end - start;
      if (bytes <= 0x5f0000 - used)
        used += bytes;
    }
  }
  return used;
}
