#pragma once
#include "wifi_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#define WIFI_LINK_CONNECTED BIT0
#define WIFI_LINK_DISCONNECTED BIT1

/* Serial worker owns connection attempts; event-loop updates use the same lock. */
void wifi_link_cancel(bool cancel);
void wifi_link_restore_state(wifi_status_t *status);
wifi_result_t wifi_link_connect(const wifi_saved_network_t *network,
                               wifi_status_t *status, SemaphoreHandle_t lock,
                               EventGroupHandle_t events, char *detail, size_t detail_size);

/* Retry only transient AUTH_EXPIRE (reason 2), at most three attempts total. */
wifi_result_t wifi_link_connect_retry(const wifi_saved_network_t *network,
                                     wifi_status_t *status, SemaphoreHandle_t lock,
                                     EventGroupHandle_t events, char *detail, size_t detail_size);

/* Called under the status lock; stop-related events must not re-enable Wi-Fi. */
void wifi_link_disconnected(wifi_status_t*,unsigned reason,bool paused);
