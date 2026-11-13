#pragma once
#include "partitions_mgr.h"
#include <stddef.h>
#define FLASH_IMAGE_SIZE 0x1000000u
#define FLASH_GUARD_OFFSET 0x15000u
typedef struct flash_snapshot {
  char path[512];
  uint8_t sha256[32];
} flash_snapshot_t;
/* All blocking functions are worker-only; caller serializes with installer. */
esp_err_t flash_snapshot_create(flash_snapshot_t *, partition_progress_fn,
                                void *, char *, size_t);
esp_err_t flash_snapshot_validate(const char *, flash_snapshot_t *, char *,
                                  size_t);
esp_err_t flash_snapshot_restore(const flash_snapshot_t *,
                                 partition_progress_fn, void *, char *, size_t);
esp_err_t flash_migrate_plan(const uint32_t *, unsigned, uint8_t[32], char *,
                             size_t);
esp_err_t flash_migrate_apply(const uint32_t *, unsigned, const uint8_t[32],
                              partition_progress_fn, void *, char *, size_t);
bool flash_operation_pending(void);
esp_err_t flash_operation_resume(partition_progress_fn, void *, char *, size_t);
typedef struct {
  uint8_t digest[32];
  uint32_t address, size, free_bytes, launcher_size, capacity;
  uint32_t reserved[16], minimum[16];
  int slot;
  bool changed;
} flash_allocation_t;
esp_err_t flash_allocate_plan(uint32_t required, flash_allocation_t *, char *,
                              size_t);
esp_err_t flash_allocate_apply(uint32_t required, const uint8_t[32],
                               partition_progress_fn, void *, char *, size_t);

esp_err_t flash_layout_plan(const uint32_t sizes[16], flash_allocation_t *,
                            char *, size_t);
esp_err_t flash_layout_apply(const uint32_t sizes[16], const uint8_t[32],
                             partition_progress_fn, void *, char *, size_t);

/* Unknown/corrupt guard or journal is an error, never a clean state. */
esp_err_t flash_operation_status(bool *pending);

esp_err_t flash_allocate_apply_options(uint32_t,const uint8_t[32],bool,partition_progress_fn,void*,char*,size_t);
esp_err_t flash_layout_apply_options(const uint32_t[16],const uint8_t[32],bool,partition_progress_fn,void*,char*,size_t);

esp_err_t flash_data_create(const char*,uint8_t,uint32_t,uint32_t,bool,partition_progress_fn,void*,char*,size_t);

esp_err_t flash_snapshot_restore_options(const flash_snapshot_t*,bool,partition_progress_fn,void*,char*,size_t);
