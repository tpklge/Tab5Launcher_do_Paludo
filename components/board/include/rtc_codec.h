#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
bool rtc_decode(const uint8_t data[7],time_t *utc);
bool rtc_encode(time_t utc,uint8_t data[7]);
