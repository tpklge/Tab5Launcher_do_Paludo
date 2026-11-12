/* Status widgets consume cached sensor data; no I2C or SD transactions in LVGL.
 */
#include "board.h"
#include "input.h"
#include "time_mgr.h"
#include "ui.h"
#include "wifi_mgr.h"
#include <stdio.h>
static lv_obj_t *s_time, *s_battery, *s_flow, *s_wifi, *s_sd, *s_keyboard;
static lv_obj_t *item(lv_obj_t *bar, const char *text) {
  lv_obj_t *label = lv_label_create(bar);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xCBD5E1), 0);
  return label;
}
lv_obj_t *ui_status_bar_create(lv_obj_t *parent) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_pos(bar, 112, 0);
  lv_obj_set_size(
      bar, lv_display_get_horizontal_resolution(board_display()) - 112, 40);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x101318), 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 16, 0);
  lv_obj_set_style_pad_ver(bar, 10, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  s_time = item(bar, "--:--");
  s_battery = item(bar, LV_SYMBOL_BATTERY_EMPTY " --");
  s_flow = item(bar, "Energia: --");
  s_wifi = item(bar, "Wi-Fi --");
  s_sd = item(bar, "SD --");
  s_keyboard = item(bar, "Teclado --");
  return bar;
}
void ui_status_bar_update(void) {
  char text[96];
  time_fmt_hhmm(text, sizeof(text));
  lv_label_set_text(s_time, text);
  board_power_status_t power;
  board_power_status(&power);
  if (power.valid && power.percent >= 0) {
    const char *icon = power.percent > 80   ? LV_SYMBOL_BATTERY_FULL
                       : power.percent > 50 ? LV_SYMBOL_BATTERY_3
                       : power.percent > 20 ? LV_SYMBOL_BATTERY_2
                                            : LV_SYMBOL_BATTERY_1;
    snprintf(text, sizeof(text), "%s ~%d%%  %d.%02d V", icon, power.percent,
             power.millivolts / 1000, (power.millivolts % 1000) / 10);
  } else
    snprintf(text, sizeof(text), LV_SYMBOL_BATTERY_EMPTY " Bateria --");
  lv_label_set_text(s_battery, text);
  lv_obj_set_style_text_color(
      s_battery,
      (power.valid && power.percent >= 0 && power.percent < 15
           ? lv_color_hex(0xC52137)
           : ui_text_color()),
      0);
  if (!power.valid)
    snprintf(text, sizeof(text), "Energia: sem leitura");
  else
    snprintf(text, sizeof(text), "%s %d mA",
             power.charge == BOARD_CHARGE_CHARGING ? LV_SYMBOL_CHARGE
                 " Carregando"
             : power.charge == BOARD_CHARGE_ON_BATTERY ? "Descarga"
                                                       : "Repouso",
             power.milliamps < 0 ? -power.milliamps : power.milliamps);
  lv_label_set_text(s_flow, text);
  wifi_status_t wifi;
  wifi_get_status(&wifi);
  if (wifi.state == WIFI_STATE_CONNECTED)
    snprintf(text, sizeof(text), LV_SYMBOL_WIFI " %d dBm", wifi.rssi);
  else
    snprintf(text, sizeof(text), "Wi-Fi %s",
             wifi.state == WIFI_STATE_FAILED     ? "falhou"
             : wifi.state == WIFI_STATE_DISABLED ? "off"
                                                 : "...");
  lv_label_set_text(s_wifi, text);
  if (board_sd_ready() && power.sd_total && power.sd_free != UINT64_MAX) {
    uint64_t tenths = (power.sd_free * 10 + (1ull << 29)) / (1ull << 30);
    snprintf(text, sizeof(text), LV_SYMBOL_SD_CARD " %llu.%llu GiB livres",
             (unsigned long long)(tenths / 10),
             (unsigned long long)(tenths % 10));
  } else
    snprintf(text, sizeof(text), "SD %s",
             board_sd_ready() ? "livre --" : "ausente");
  lv_label_set_text(s_sd, text);
  lv_label_set_text(s_keyboard, input_kbd_present() ? LV_SYMBOL_KEYBOARD " A164"
                                                    : "Teclado ausente");
}
