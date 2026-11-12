/* Own both transports so failed mounts cannot leak BSP LDO/bus handles. */
#include "board.h"
#include "bsp/m5stack_tab5.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
static const char *TAG = "board_sd";
static board_sd_state_t s_sd_state = BOARD_SD_UNCHECKED;
/* ---- SD ------------------------------------------------------------------ */

static sdmmc_card_t *s_sd_card;
static sd_pwr_ctrl_handle_t s_sd_power;
static SemaphoreHandle_t s_sd_mount_lock;
static esp_err_t mount_card(void) {
  const esp_vfs_fat_sdmmc_mount_config_t mount = {
      .format_if_mount_failed = false,
      .max_files = 32,
      .allocation_unit_size = 16 * 1024};
  sd_pwr_ctrl_ldo_config_t power = {.ldo_chan_id = 4};
  esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&power, &s_sd_power);
  if (err)
    return err;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;
  host.pwr_ctrl_handle = s_sd_power;
  sdmmc_slot_config_t slot;
  bsp_sdcard_sdmmc_get_slot(SDMMC_HOST_SLOT_0, &slot);
  err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &s_sd_card);
  if (!err) {
    ESP_LOGI(TAG, "SD montado: SDMMC 4-bit / 20 MHz");
    return ESP_OK;
  }
  /* A card left in SPI mode by another firmware cannot return to native mode
   * through CMD0 alone. A CPU reset does not necessarily power-cycle the card.
   * Retain the shared I/O supply and try SPI after native host cleanup. */
  ESP_LOGW(TAG, "SDMMC falhou (%s); tentando SD via SPI", esp_err_to_name(err));
  const spi_bus_config_t bus = {.mosi_io_num = BSP_SD_SPI_MOSI,
                                .miso_io_num = BSP_SD_SPI_MISO,
                                .sclk_io_num = BSP_SD_SPI_CLK,
                                .quadwp_io_num = -1,
                                .quadhd_io_num = -1,
                                .data4_io_num = -1,
                                .data5_io_num = -1,
                                .data6_io_num = -1,
                                .data7_io_num = -1,
                                .max_transfer_sz = 4096};
  err = spi_bus_initialize(BSP_SDSPI_HOST, &bus, SPI_DMA_CH_AUTO);
  if (!err) {
    sdmmc_host_t spi = SDSPI_HOST_DEFAULT();
    spi.slot = BSP_SDSPI_HOST;
    spi.max_freq_khz = 10000;
    spi.pwr_ctrl_handle = s_sd_power;
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = BSP_SDSPI_HOST;
    device.gpio_cs = BSP_SD_SPI_CS;
    err = esp_vfs_fat_sdspi_mount("/sdcard", &spi, &device, &mount, &s_sd_card);
    if (!err) {
      ESP_LOGI(TAG, "SD recuperado: SPI / 10 MHz");
      return ESP_OK;
    }
    spi_bus_free(BSP_SDSPI_HOST);
  }
  ESP_LOGW(TAG, "SD SPI também falhou: %s", esp_err_to_name(err));
  sd_pwr_ctrl_del_on_chip_ldo(s_sd_power);
  s_sd_power = NULL;
  s_sd_card = NULL;
  return err;
}
esp_err_t board_sd_mount(void) {
  if (!s_sd_mount_lock)
    s_sd_mount_lock = xSemaphoreCreateMutex();
  if (!s_sd_mount_lock)
    return ESP_ERR_NO_MEM;
  xSemaphoreTake(s_sd_mount_lock, portMAX_DELAY);
  esp_err_t err = ESP_OK;
  if (s_sd_state != BOARD_SD_MOUNTED) {
    err = mount_card();
    s_sd_state = err == ESP_OK              ? BOARD_SD_MOUNTED
                 : err == ESP_ERR_NOT_FOUND ? BOARD_SD_NO_CARD
                 : err == ESP_FAIL          ? BOARD_SD_UNREADABLE
                                            : BOARD_SD_ERROR;
  }
  xSemaphoreGive(s_sd_mount_lock);
  return err;
}

bool board_sd_ready(void) { return s_sd_state == BOARD_SD_MOUNTED; }
board_sd_state_t board_sd_state(void) { return s_sd_state; }

uint64_t board_sd_total_bytes(void) {
  if (!board_sd_ready())
    return 0;
  sdmmc_card_t *card = s_sd_card;
  if (!card)
    return 0;
  return (uint64_t)card->csd.capacity * card->csd.sector_size;
}

uint64_t board_sd_free_bytes(void) {
  uint64_t total = 0, free_bytes = 0;
  return board_sd_ready() &&
                 esp_vfs_fat_info("/sdcard", &total, &free_bytes) == ESP_OK
             ? free_bytes
             : UINT64_MAX;
}

const char *board_sd_status(bool detail) {
  switch (s_sd_state) {
  case BOARD_SD_MOUNTED:
    return "OK";
  case BOARD_SD_NO_CARD:
    return detail ? "No SD card — insert a card and try again" : "NO CARD";
  case BOARD_SD_UNREADABLE:
    return detail ? "Card not readable — check FAT32/exFAT format"
                  : "UNREADABLE";
  case BOARD_SD_ERROR:
    return detail ? "SD error — check card seating" : "ERROR";
  default:
    return "UNCHECKED";
  }
}

#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
esp_err_t board_sd_export_card(sdmmc_card_t **out) {
  if (!out)
    return ESP_ERR_INVALID_ARG;
  esp_err_t e = board_sd_mount();
  if (e)
    return e;
  BYTE drive = ff_diskio_get_pdrv_card(s_sd_card);
  if (drive == 0xff)
    return ESP_FAIL;
  char volume[4] = {(char)('0' + drive), ':', 0};
  if (f_mount(NULL, volume, 0) != FR_OK)
    return ESP_FAIL;
  e = esp_vfs_fat_unregister_path("/sdcard");
  if (e)
    return e;
  ff_diskio_unregister(drive);
  s_sd_state = BOARD_SD_UNCHECKED;
  *out = s_sd_card;
  return ESP_OK;
}
