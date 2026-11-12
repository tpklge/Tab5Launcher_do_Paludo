#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {bool started,mounted,ejecting;uint64_t total,free;char error[80];}usb_storage_status_t;
static inline void usb_storage_status(usb_storage_status_t*s){*s=(usb_storage_status_t){0};}
static inline esp_err_t usb_storage_eject(void){return 0;}
