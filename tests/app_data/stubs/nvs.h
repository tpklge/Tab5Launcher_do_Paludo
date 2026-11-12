#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef unsigned nvs_handle_t;typedef void*nvs_iterator_t;typedef unsigned nvs_type_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define NVS_TYPE_ANY 0xff
#define NVS_TYPE_U8 1
#define NVS_TYPE_I8 0x11
#define NVS_TYPE_U16 2
#define NVS_TYPE_I16 0x12
#define NVS_TYPE_U32 4
#define NVS_TYPE_I32 0x14
#define NVS_TYPE_U64 8
#define NVS_TYPE_I64 0x18
#define NVS_TYPE_STR 0x21
#define NVS_TYPE_BLOB 0x42
typedef struct{char namespace_name[16],key[16];nvs_type_t type;}nvs_entry_info_t;
int nvs_open(const char*,int,nvs_handle_t*);void nvs_close(nvs_handle_t);int nvs_commit(nvs_handle_t);int nvs_erase_all(nvs_handle_t);
int nvs_entry_find(const char*,const char*,nvs_type_t,nvs_iterator_t*);int nvs_entry_next(nvs_iterator_t*);int nvs_entry_info(nvs_iterator_t,nvs_entry_info_t*);void nvs_release_iterator(nvs_iterator_t);
int nvs_get_str(nvs_handle_t,const char*,char*,size_t*);int nvs_set_str(nvs_handle_t,const char*,const char*);int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_get_u8(nvs_handle_t,const char*,uint8_t*);int nvs_set_u8(nvs_handle_t,const char*,uint8_t);
int nvs_get_i8(nvs_handle_t,const char*,int8_t*);int nvs_set_i8(nvs_handle_t,const char*,int8_t);
int nvs_get_u16(nvs_handle_t,const char*,uint16_t*);int nvs_set_u16(nvs_handle_t,const char*,uint16_t);
int nvs_get_i16(nvs_handle_t,const char*,int16_t*);int nvs_set_i16(nvs_handle_t,const char*,int16_t);
int nvs_get_u32(nvs_handle_t,const char*,uint32_t*);int nvs_set_u32(nvs_handle_t,const char*,uint32_t);
int nvs_get_i32(nvs_handle_t,const char*,int32_t*);int nvs_set_i32(nvs_handle_t,const char*,int32_t);
int nvs_get_u64(nvs_handle_t,const char*,uint64_t*);int nvs_set_u64(nvs_handle_t,const char*,uint64_t);
int nvs_get_i64(nvs_handle_t,const char*,int64_t*);int nvs_set_i64(nvs_handle_t,const char*,int64_t);
