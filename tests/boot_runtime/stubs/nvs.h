#pragma once
#include <stdint.h>
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int nvs_handle_t;
int nvs_open(const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
int nvs_commit(nvs_handle_t);
int nvs_get_i8(nvs_handle_t,const char*,int8_t*);
int nvs_set_i8(nvs_handle_t,const char*,int8_t);
int nvs_get_u8(nvs_handle_t,const char*,uint8_t*);
int nvs_set_u8(nvs_handle_t,const char*,uint8_t);
int nvs_erase_key(nvs_handle_t,const char*);
