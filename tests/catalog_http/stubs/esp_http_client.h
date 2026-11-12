#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void *esp_http_client_handle_t;
typedef struct {int event_id;void *user_data;char *header_key,*header_value;} esp_http_client_event_t;
#define HTTP_EVENT_ON_HEADER 1
typedef struct {int(*event_handler)(esp_http_client_event_t*);void*user_data;const char *url;int timeout_ms,buffer_size,buffer_size_tx;int(*crt_bundle_attach)(void*);bool disable_auto_redirect;} esp_http_client_config_t;
void *esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_open(void*,int);
int64_t esp_http_client_fetch_headers(void*);
int esp_http_client_get_status_code(void*);
int esp_http_client_set_redirection(void*);
int esp_http_client_get_url(void*,char*,int);
int esp_http_client_close(void*);
int esp_http_client_cleanup(void*);
int esp_http_client_read(void*,char*,int);
bool esp_http_client_is_complete_data_received(void*);

int64_t esp_http_client_get_content_length(void*);

#define ESP_ERR_HTTP_EAGAIN 100

int esp_http_client_set_header(void*,const char*,const char*);
