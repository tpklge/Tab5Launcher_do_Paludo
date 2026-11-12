#pragma once
#include <stdint.h>
#include "esp_err.h"
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_WPA2_WPA3_PSK 7
#define WPA3_SAE_PWE_BOTH 2
#define WIFI_IF_STA 0
typedef struct {
    struct { uint8_t ssid[32], password[64]; struct { uint8_t authmode; } threshold; int sae_pwe_h2e; } sta;
} wifi_config_t;
typedef struct { int rssi; } wifi_ap_record_t;
int esp_wifi_set_config(int,const wifi_config_t *);
int esp_wifi_connect(void);
int esp_wifi_disconnect(void);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
#define WIFI_REASON_AUTH_EXPIRE 2
