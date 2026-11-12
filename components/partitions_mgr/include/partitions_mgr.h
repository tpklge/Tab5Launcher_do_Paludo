#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
typedef bool (*partition_progress_fn)(unsigned,const char*,void*);
esp_err_t partitions_mgr_init(void);
esp_err_t partitions_mgr_big_plan(uint8_t table_sha[32]);
/* Caller holds installer lock, has explicit consent and durable resume record. */
esp_err_t partitions_mgr_apply_big(const uint8_t expected[32],partition_progress_fn cb,void *user,char *message,unsigned size);
esp_err_t partitions_mgr_profile_plan(const uint32_t *sizes,unsigned count,uint8_t table_sha[32]);
esp_err_t partitions_mgr_apply_profile(const uint8_t expected[32],const uint32_t*sizes,unsigned count,partition_progress_fn,void*,char*,unsigned);
