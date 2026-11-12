#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef bool (*layout_md5_fn)(const uint8_t *,size_t,uint8_t[16]);
/* Specific checked profile: adjacent app0/app1 4 MB -> app0 8 MB. */
bool layout_build_big(const uint8_t old_table[4096],uint8_t new_table[4096],layout_md5_fn md5);
/* Editable 8 MiB app bank; every entry outside it is preserved verbatim. */
bool layout_build_profile(const uint8_t old_table[4096],uint8_t new_table[4096],const uint32_t *sizes,unsigned count,layout_md5_fn md5);
/* Finds a fitting destination for every occupied app; UINT32_MAX means unused. */
bool layout_assign_apps(const uint32_t *used,unsigned old_count,const uint32_t *sizes,unsigned new_count,int *destination);
#define LAYOUT_APP_LIMIT 16
#define LAYOUT_DATA_START 0xa10000u
uint32_t layout_factory_size(const uint8_t table[4096]);
/* Sizes are indexed by OTA id; zero removes an empty entry. No fixed slots. */
bool layout_build_compact(const uint8_t old[4096],uint8_t out[4096],uint32_t launcher_size,const uint32_t sizes[LAYOUT_APP_LIMIT],layout_md5_fn);
