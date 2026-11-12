#pragma once
#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
int nvs_open(const char*,int,nvs_handle_t*);
void nvs_close(nvs_handle_t);
int nvs_get_str(nvs_handle_t,const char*,char*,size_t*);
