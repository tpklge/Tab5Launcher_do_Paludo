#pragma once
#include "esp_err.h"
#include <stdbool.h>
esp_err_t usb_disk_request(void);
bool usb_disk_boot_requested(void);
void usb_disk_run(void);

#include <stdint.h>
void usb_disk_last_status(uint32_t *phase,uint32_t *seconds);

bool usb_disk_returned(void);

void usb_disk_restore_console(void);

void usb_disk_restart(void);

/* Called by app integration before accepting asynchronous mode requests. */
void usb_disk_set_transition_check(bool (*allowed)(void));
esp_err_t usb_disk_transition_error(void);
