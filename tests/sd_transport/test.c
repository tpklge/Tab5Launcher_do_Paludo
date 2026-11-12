#include "sdk.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int native_error,spi_error,bus_error,power_live,bus_live,native_calls,spi_calls;
static sdmmc_card_t card={.csd={1000,512}};
void*bsp_sdcard_sdmmc_get_slot(int n,sdmmc_slot_config_t*s){assert(n==0);s->width=4;return s;}
int sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t*p,void**out){assert(!power_live&&p->ldo_chan_id==4);power_live=1;*out=(void*)1;return 0;}
int sd_pwr_ctrl_del_on_chip_ldo(void*p){assert(power_live&&p==(void*)1);power_live=0;return 0;}
int spi_bus_initialize(int n,const spi_bus_config_t*b,int dma){assert(n==3&&!bus_live&&b->mosi_io_num==44&&b->miso_io_num==39&&b->sclk_io_num==43);if(bus_error)return bus_error;bus_live=1;return 0;}
int spi_bus_free(int n){assert(n==3&&bus_live);bus_live=0;return 0;}
void*xSemaphoreCreateMutex(void){return (void*)2;}
int xSemaphoreTake(void*p,unsigned n){assert(p==(void*)2);return 1;}
int xSemaphoreGive(void*p){assert(p==(void*)2);return 1;}
int esp_vfs_fat_info(const char*p,uint64_t*t,uint64_t*f){*t=512000;*f=128000;return 0;}
const char*esp_err_to_name(int e){return "test";}
int esp_vfs_fat_sdmmc_mount(const char*p,const sdmmc_host_t*h,const sdmmc_slot_config_t*s,const esp_vfs_fat_sdmmc_mount_config_t*m,sdmmc_card_t**out){native_calls++;assert(power_live&&h->pwr_ctrl_handle==(void*)1&&h->slot==0&&h->max_freq_khz==20000&&h->flags==32&&s->width==4&&!m->format_if_mount_failed&&m->max_files==32);if(!native_error)*out=&card;return native_error;}
int esp_vfs_fat_sdspi_mount(const char*p,const sdmmc_host_t*h,const sdspi_device_config_t*d,const esp_vfs_fat_sdmmc_mount_config_t*m,sdmmc_card_t**out){spi_calls++;assert(power_live&&bus_live&&h->pwr_ctrl_handle==(void*)1&&h->slot==3&&h->max_freq_khz==10000&&d->gpio_cs==42&&!m->format_if_mount_failed);if(!spi_error)*out=&card;return spi_error;}
static int detached;
unsigned char ff_diskio_get_pdrv_card(const sdmmc_card_t *p){assert(p==&card);return 0;}
int f_mount(void *fs,const char *volume,int opt){assert(!fs&&!strcmp(volume,"0:"));detached=1;return 0;}
int esp_vfs_fat_unregister_path(const char *p){assert(detached&&!strcmp(p,"/sdcard"));detached=2;return 0;}
void ff_diskio_unregister(unsigned char d){assert(!d&&detached==2);detached=3;}
#include "../../components/board/board_sd.c"
static void reset(void){power_live=bus_live=native_calls=spi_calls=0;s_sd_state=BOARD_SD_UNCHECKED;s_sd_card=NULL;s_sd_power=NULL;native_error=spi_error=bus_error=0;}
int main(void){
 reset();assert(board_sd_mount()==0&&board_sd_ready()&&native_calls==1&&!spi_calls);assert(board_sd_total_bytes()==512000&&board_sd_free_bytes()==128000);assert(board_sd_mount()==0&&native_calls==1);
 reset();native_error=ESP_ERR_TIMEOUT;assert(board_sd_mount()==0&&board_sd_ready()&&native_calls==1&&spi_calls==1&&power_live&&bus_live);
 reset();native_error=ESP_ERR_TIMEOUT;spi_error=ESP_ERR_TIMEOUT;assert(board_sd_mount()!=0&&!board_sd_ready()&&!power_live&&!bus_live);assert(board_sd_total_bytes()==0&&board_sd_free_bytes()==UINT64_MAX);spi_error=0;assert(board_sd_mount()==0&&board_sd_ready());
 reset();native_error=ESP_FAIL;bus_error=ESP_FAIL;assert(board_sd_mount()!=0&&!power_live&&!bus_live&&!spi_calls);
 reset();sdmmc_card_t *exported=NULL;assert(board_sd_export_card(&exported)==0&&exported==&card&&detached==3&&!board_sd_ready()&&power_live);
 puts("SD transport: native mount, retained-SPI fallback, no formatting, bus/LDO ownership, failed cleanup and retry passed.");
}
