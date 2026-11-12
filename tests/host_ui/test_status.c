#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "board.h"
#include "wifi_mgr.h"
static board_power_status_t power={.valid=true,.millivolts=8291,.milliamps=1,.percent=94,.charge=BOARD_CHARGE_IDLE,.sd_total=64ull*1024*1024*1024,.sd_free=323ull*1024*1024*1024/10};
void board_power_status(board_power_status_t *out) { *out=power; }
bool board_sd_ready(void) { return true; }
lv_display_t *board_display(void) { return lv_display_get_default(); }
void wifi_get_status(wifi_status_t *s) { *s=(wifi_status_t){.state=WIFI_STATE_CONNECTED,.rssi=-45}; }
bool input_kbd_present(void) { return true; }
void time_fmt_hhmm(char *b,size_t n) { snprintf(b,n,"12:34"); }
#include "../../components/ui/ui_status_bar.c"
extern void ui_theme_apply(ui_theme_t,uint32_t);
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *b) { lv_display_flush_ready(d); }
int main(void)
{
 lv_init(); lv_display_t *d=lv_display_create(1280,720); static uint8_t pixels[1280*20*4]; lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL); lv_display_set_flush_cb(d,flush);
 ui_status_bar_create(lv_screen_active()); ui_theme_apply(UI_THEME_DAYLIGHT,0xffffff); ui_status_bar_update();
 assert(strstr(lv_label_get_text(s_sd),"32.3 GiB")); assert(strstr(lv_label_get_text(s_battery),"94%")); assert(strstr(lv_label_get_text(s_flow),"Repouso"));
 lv_obj_update_layout(lv_screen_active()); lv_area_t area; lv_obj_get_coords(s_keyboard,&area); assert(area.x2<1280);
 assert(lv_color_to_u32(lv_obj_get_style_text_color(s_battery,0))==lv_color_to_u32(ui_text_color()));
 power.sd_free=UINT64_MAX; ui_status_bar_update(); assert(strstr(lv_label_get_text(s_sd),"--")&&!strstr(lv_label_get_text(s_sd),"0.0"));
 power.sd_free=0; ui_status_bar_update(); assert(strstr(lv_label_get_text(s_sd),"0.0"));
 power.valid=false; ui_status_bar_update(); assert(strstr(lv_label_get_text(s_battery),"--")); assert(strstr(lv_label_get_text(s_flow),"sem leitura"));
 lv_deinit(); puts("Status: real cached free-space formatting, unknown vs zero, battery/flow, Daylight contrast and 1280px geometry passed (ASan/UBSan).");
}
