#pragma once
#include <stdbool.h>
#include <stdint.h>
static bool board_sd_ready(void){return true;}
static uint64_t board_sd_free_bytes(void){return UINT64_MAX;}
