#pragma once
#include <stddef.h>
typedef void esp_console_repl_t;
typedef struct {unsigned task_stack_size,max_history_len;const char *prompt;size_t max_cmdline_length;} esp_console_repl_config_t;
#define ESP_CONSOLE_REPL_CONFIG_DEFAULT() {0}
typedef int esp_console_dev_usb_serial_jtag_config_t;
#define ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT() 0
typedef struct {const char *command,*help;int (*func)(int,char**);} esp_console_cmd_t;
static int esp_console_new_repl_usb_serial_jtag(const void*a,const void*b,void*c){return 0;}
static int esp_console_cmd_register(void*p){return 0;}
static int esp_console_start_repl(void*p){return 0;}
