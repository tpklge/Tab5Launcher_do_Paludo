#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {bool started,usb,online,bye;char host[32],version[16],ip[40],error[128];} pi_link_status_t;
esp_err_t pi_link_init(void);
void pi_link_status(pi_link_status_t *out);
/* Tab5Link text/raw service framing, one bounded call at a time, worker only.
 * A compiled protocol implementation is not a claim of physical Pi validation. */
esp_err_t pi_link_call(const char *service,const void *request,size_t length,void *body,size_t capacity,size_t *received,int *status,char *meta,size_t meta_size);
esp_err_t pi_link_shutdown(void);
esp_err_t pi_link_sync_time(void);

/* Handler runs on the Pi worker: it must not call pi_link_call itself.
 * Return a malloc-owned response, freed after transmission. */
typedef void (*pi_ask_handler_fn)(const char*,const char*,uint8_t**,size_t*,int*);
void pi_link_set_ask_handler(pi_ask_handler_fn);
