#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef void *cdc_acm_dev_hdl_t;
typedef struct {int type;} cdc_acm_host_dev_event_data_t;
typedef struct {unsigned connection_timeout_ms,out_buffer_size,in_buffer_size;void (*event_cb)(const cdc_acm_host_dev_event_data_t*,void*);bool (*data_cb)(const uint8_t*,size_t,void*);} cdc_acm_host_device_config_t;
typedef struct {bool skip_phy_setup;int intr_flags;} usb_host_config_t;
#define ESP_INTR_FLAG_LOWMED 1
#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 1
#define CDC_ACM_HOST_DEVICE_DISCONNECTED 1
#define CDC_ACM_HOST_ERROR 2
#define CDC_HOST_ANY_VID 0
#define CDC_HOST_ANY_PID 0
static int cdc_acm_host_data_tx_blocking(void*d,const void*b,size_t n,unsigned t){return 0;}
static int cdc_acm_host_close(void *d){return 0;}
static int cdc_acm_host_open(int a,int b,int c,void*d,void*e){return 1;}
static int cdc_acm_host_set_control_line_state(void *d,bool a,bool b){return 0;}
static int cdc_acm_host_install(void *c){return 0;}
static int usb_host_lib_handle_events(unsigned t,uint32_t *f){return 0;}
static int usb_host_device_free_all(void){return 0;}
static int usb_host_install(const usb_host_config_t *c){return 0;}
static int usb_host_uninstall(void){return 0;}
