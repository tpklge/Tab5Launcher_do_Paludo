#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool started,mounted,ejecting;uint64_t total,free;char error[160];} usb_storage_status_t;
esp_err_t usb_storage_init(void);
void usb_storage_status(usb_storage_status_t *);
esp_err_t usb_storage_eject(void);
