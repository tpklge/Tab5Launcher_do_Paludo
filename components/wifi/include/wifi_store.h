#pragma once
#include "wifi_mgr.h"

typedef struct {
    char ssid[33];
    char password[64];
    uint8_t auth;
} wifi_saved_network_t;

/* Initialize once before UI/worker start. Reads never print credentials. */
esp_err_t wifi_saved_init(void);
esp_err_t wifi_store_put(const wifi_saved_network_t *network);
bool wifi_store_latest(wifi_saved_network_t *network);
