/* board.c — hardware init for the M5Stack Tab5.
 *
 * Uses the M5Stack Tab5 BSP (espressif/m5stack_tab5 >=1.3.2).
 * PI4IOE 5V0674 expander at 0x44 controls C6 power, USB-A 5V, charger enable,
 * power-off.
 *
 * Bus ownership:
 *   MIPI-DSI       — BSP / display driver (exclusive)
 *   I²C port 0     — BSP (touch, expanders); acquired via bsp_i2c_get_handle()
 *   SDMMC slot 0   — SD card (bsp_sdcard_mount)
 *   SDIO slot 1    — ESP-Hosted (ESP32-C6), owned by wifi component
 */
#include "board.h"
#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char *TAG = "board";

/* ---- State --------------------------------------------------------------- */

static lv_display_t *s_display = NULL;
static board_rev_t s_rev = BOARD_REV_UNKNOWN;

static bool s_usb5v_on = false;

/* Power control via BSP IO expanders.
 * The Tab5 has two PI4IOE expanders managed by the BSP:
 *   expander0 (ADDRESS_LOW): LCD, touch, speaker, camera
 *   expander1 (ADDRESS_HIGH): Wi-Fi/C6 power (BSP_WIFI_EN), USB5V (BSP_USB_EN)
 * We use bsp_feature_enable() for C6 and USB; charger is a stub until pin
 * mapping confirmed. */

/* ---- Init ---------------------------------------------------------------- */

esp_err_t board_display_init(void) {
  if (s_display)
    return ESP_OK;
  ESP_LOGI(TAG, "board_display_init");
  /* Blank panel initialization, including a GPIO held through an MSC restart.
   */
  gpio_set_direction(BSP_LCD_BACKLIGHT, GPIO_MODE_OUTPUT);
  gpio_set_level(BSP_LCD_BACKLIGHT, 0);
  gpio_hold_en(BSP_LCD_BACKLIGHT);

  /* BSP init: display (MIPI-DSI), touch, I2C bus */
  bsp_display_cfg_t disp_cfg = {
      .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
      .buffer_size = BSP_LCD_H_RES * 100, /* ~100 lines buffer */
      .double_buffer = true,
      .flags =
          {
              .buff_spiram = true, /* allocate display buffer in PSRAM */
              .sw_rotate = true, /* software rotation: ST7123 DPI panel doesn't
                                    support swap_xy */
          },
  };
  /* UI payloads stay on the heap; allow headroom for LVGL/dialog call chains.
   */
  disp_cfg.lvgl_port_cfg.task_stack = 12288;
  s_display = bsp_display_start_with_config(&disp_cfg);
  if (!s_display) {
    ESP_LOGE(TAG, "BSP display start failed");
    return ESP_FAIL;
  }

  /* Tab5 panel is physically portrait (720x1280) but used landscape.
   * Rotate 270° (CCW) so LVGL coordinate system becomes 1280x720.
   * 270° aligns both render and touch correctly for USB-port-at-bottom
   * orientation. */
  if (!board_lvgl_lock(2000))
    return ESP_ERR_TIMEOUT;
  bsp_display_rotate(s_display, LV_DISPLAY_ROTATION_90);
  board_lvgl_unlock();

  /* Keep backlight off until the first complete UI frame is rendered. */
  bsp_display_brightness_set(0);
  gpio_hold_dis(BSP_LCD_BACKLIGHT);

  /* Board revision: BSP detected ST7123 (v2) at runtime */
  s_rev = BOARD_REV_V2_ST7123;
  ESP_LOGI(TAG, "board rev: %s", board_rev_name(s_rev));

  return ESP_OK;
}
esp_err_t board_init(void) {
  esp_err_t e = board_display_init();
  if (e)
    return e;
  /* SD card (non-fatal) */
  if (board_sd_mount() != ESP_OK) {
    ESP_LOGW(TAG, "SD not mounted at boot: %s", board_sd_status(false));
  }

  board_rtc_init();
  board_power_init();

  ESP_LOGI(TAG, "board_init done, free internal=%u B",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  return ESP_OK;
}

void board_shutdown(void) {
  ESP_LOGI(TAG, "board_shutdown");
  board_backlight(0);
}

/* ---- Display / LVGL ------------------------------------------------------ */

lv_display_t *board_display(void) { return s_display; }

bool board_lvgl_lock(uint32_t timeout_ms) {
  return lvgl_port_lock(timeout_ms == 0 ? portMAX_DELAY : timeout_ms);
}

void board_lvgl_unlock(void) { lvgl_port_unlock(); }

void board_backlight(uint8_t percent) {
  bsp_display_brightness_set((int)percent);
}

void board_display_guard_restart(void) {
  if (s_display) {
    ledc_stop(LEDC_LOW_SPEED_MODE, CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH, 0);
    gpio_hold_en(BSP_LCD_BACKLIGHT);
  }
}

void board_reveal(uint8_t percent) {
  if (board_lvgl_lock(2000)) {
    lv_refr_now(s_display);
    board_lvgl_unlock();
  }
  board_backlight(percent);
}

bool board_touch_pressed(void) {
  if (!board_lvgl_lock(100))
    return false;
  bool pressed = false;
  lv_indev_t *indev = lv_indev_get_next(NULL);
  while (indev) {
    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER &&
        lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
      pressed = true;
      break;
    }
    indev = lv_indev_get_next(indev);
  }
  board_lvgl_unlock();
  return pressed;
}

void board_set_rotation(uint16_t degrees) {
  if (!s_display)
    return;
  lv_disp_rotation_t rot =
      (degrees == 180) ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0;
  if (board_lvgl_lock(500)) {
    lv_display_set_rotation(s_display, rot);
    board_lvgl_unlock();
  }
}

board_rev_t board_get_rev(void) { return s_rev; }

const char *board_rev_name(board_rev_t rev) {
  switch (rev) {
  case BOARD_REV_V1_ILI9881C:
    return "ILI9881C+GT911";
  case BOARD_REV_V2_ST7123:
    return "ST7123+ST712x";
  default:
    return "unknown";
  }
}

/* ---- RTC ----------------------------------------------------------------- */

/* ---- Battery / power ----------------------------------------------------- */

int board_battery_percent(void) {
  board_power_status_t status;
  board_power_status(&status);
  return status.valid ? status.percent : -1;
}
board_charge_t board_charge_state(void) {
  board_power_status_t status;
  board_power_status(&status);
  return status.valid ? status.charge : BOARD_CHARGE_UNKNOWN;
}

esp_err_t board_charger_set(bool enable) {
  esp_io_expander_handle_t io = bsp_io_expander1_init();
  if (!io)
    return ESP_FAIL;
  uint32_t pin = 1u << 7; /* IOE1 G7 CHG_EN, official Tab5 mapping. */
  esp_err_t err = esp_io_expander_set_level(io, pin, enable);
  if (err == ESP_OK)
    err = esp_io_expander_set_output_mode(io, pin,
                                          IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
  if (err == ESP_OK)
    err = esp_io_expander_set_dir(io, pin, IO_EXPANDER_OUTPUT);
  return err;
}
bool board_charger_enabled(void) {
  esp_io_expander_handle_t io = bsp_io_expander1_init();
  uint32_t level = 0;
  return io && esp_io_expander_get_level(io, 1u << 7, &level) == ESP_OK &&
         (level & (1u << 7));
}
esp_err_t board_charger_on(void) { return board_charger_set(true); }
esp_err_t board_power_off(void) {
  esp_io_expander_handle_t io = bsp_io_expander1_init();
  if (!io)
    return ESP_FAIL;
  const uint32_t pin =
      1u << 4; /* IOE1 G4 PWROFF_PULSE. USB supply may keep board on. */
  esp_err_t err = esp_io_expander_set_level(io, pin, 0);
  if (err == ESP_OK)
    err = esp_io_expander_set_output_mode(io, pin,
                                          IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
  if (err == ESP_OK)
    err = esp_io_expander_set_dir(io, pin, IO_EXPANDER_OUTPUT);
  if (err != ESP_OK)
    return err;
  for (unsigned i = 0; i < 10; i++) {
    err = esp_io_expander_set_level(io, pin, i & 1);
    if (err != ESP_OK)
      return err;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  esp_io_expander_set_level(io, pin, 0);
  return ESP_ERR_INVALID_STATE; /* still powered */
}

esp_err_t board_c6_power(bool on) {
  return bsp_feature_enable(BSP_FEATURE_WIFI, on);
}

esp_err_t board_usb5v(bool on) {
  esp_err_t err = bsp_feature_enable(BSP_FEATURE_USB, on);
  if (err == ESP_OK)
    s_usb5v_on = on;
  return err;
}

bool board_usb5v_on(void) { return s_usb5v_on; }
