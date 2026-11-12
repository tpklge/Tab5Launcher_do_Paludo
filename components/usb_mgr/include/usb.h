#pragma once
#include "esp_err.h"
/* USB Serial/JTAG recovery console. Independent of USB-A host and Wi-Fi. */
esp_err_t usb_init(void);

#include <stdbool.h>
bool usb_transfer_active(void);

void usb_set_handoff_blocked(bool blocked);

#include "usb_disk.h"

esp_err_t usb_update_c6(const char *sha256);

void usb_set_view_handler(esp_err_t (*handler)(const char *));
