#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_ERR_WIFI_NOT_CONNECT 5
#define ESP_ERR_NVS_INVALID_LENGTH 6
const char *esp_err_to_name(int err);
#define ESP_ERR_NOT_SUPPORTED 7
#define ESP_ERR_NOT_FOUND 8

#define ESP_ERR_INVALID_SIZE 9
#define ESP_ERR_INVALID_CRC 10
