#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_NOT_FOUND 2
#define ESP_ERR_TIMEOUT 0x107
#define portMAX_DELAY UINT_MAX
#define SDMMC_HOST_SLOT_0 0
#define BSP_SDSPI_HOST 3
#define SDMMC_FREQ_DEFAULT 20000
#define SPI_DMA_CH_AUTO 1
#define BSP_SD_SPI_MOSI 44
#define BSP_SD_SPI_MISO 39
#define BSP_SD_SPI_CLK 43
#define BSP_SD_SPI_CS 42
#define SDMMC_HOST_DEFAULT() {.flags=32}
#define SDSPI_HOST_DEFAULT() {.flags=64}
#define SDSPI_DEVICE_CONFIG_DEFAULT() {0}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef enum {BOARD_SD_UNCHECKED,BOARD_SD_MOUNTED,BOARD_SD_NO_CARD,BOARD_SD_UNREADABLE,BOARD_SD_ERROR}board_sd_state_t;
typedef struct {struct {uint32_t capacity,sector_size;}csd;}sdmmc_card_t;
typedef void *sd_pwr_ctrl_handle_t;
typedef void *SemaphoreHandle_t;
typedef struct {int ldo_chan_id;}sd_pwr_ctrl_ldo_config_t;
typedef struct {bool format_if_mount_failed;int max_files,allocation_unit_size;}esp_vfs_fat_sdmmc_mount_config_t;
typedef struct {int slot,max_freq_khz,flags;void*pwr_ctrl_handle;}sdmmc_host_t;
typedef struct {int width;}sdmmc_slot_config_t;
typedef struct {int host_id,gpio_cs;}sdspi_device_config_t;
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,data4_io_num,data5_io_num,data6_io_num,data7_io_num,max_transfer_sz;}spi_bus_config_t;
void*bsp_sdcard_sdmmc_get_slot(int,sdmmc_slot_config_t*);
int esp_vfs_fat_sdmmc_mount(const char*,const sdmmc_host_t*,const sdmmc_slot_config_t*,const esp_vfs_fat_sdmmc_mount_config_t*,sdmmc_card_t**);
int esp_vfs_fat_sdspi_mount(const char*,const sdmmc_host_t*,const sdspi_device_config_t*,const esp_vfs_fat_sdmmc_mount_config_t*,sdmmc_card_t**);
int sd_pwr_ctrl_new_on_chip_ldo(const sd_pwr_ctrl_ldo_config_t*,void**);
int sd_pwr_ctrl_del_on_chip_ldo(void*);
int spi_bus_initialize(int,const spi_bus_config_t*,int);
int spi_bus_free(int);
void*xSemaphoreCreateMutex(void);
int xSemaphoreTake(void*,unsigned);
int xSemaphoreGive(void*);
int esp_vfs_fat_info(const char*,uint64_t*,uint64_t*);
const char*esp_err_to_name(int);

#define ESP_ERR_INVALID_ARG 258
int esp_vfs_fat_unregister_path(const char*);
