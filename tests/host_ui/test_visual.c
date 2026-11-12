#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "storage.h"
#include "ui_nav.h"
#include "time_mgr.h"
static launcher_config_t config={.bright=80,.dim_s=1,.screen_off_s=2,.idle_clock=true};
static bool critical;
static unsigned light;
static lv_point_t touch_point;
static bool touch_down;
static unsigned underneath_clicks, invalidations;
static void invalidated(lv_event_t *e) { invalidations++; }
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data) { data->point=touch_point;data->state=touch_down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED; }
static void clicked(lv_event_t *e) { underneath_clicks++; }
const launcher_config_t *storage_get_config(void) { return &config; }
bool installer_is_busy(void) { return critical; }
bool ui_services_busy(void) {return false;}
bool ui_sd_busy(void) { return false; }
bool ui_progress_visible(void) { return false; }
void board_backlight(uint8_t b) { light=b; }
lv_display_t *board_display(void) { return lv_display_get_default(); }
bool time_is_valid(void) { return true; }
void time_fmt_hhmm(char *b,size_t n) { snprintf(b,n,"12:34"); }
void time_fmt_rest(char *b,size_t n) { struct tm t={.tm_year=124,.tm_mon=1,.tm_mday=29,.tm_wday=4}; time_date_pt(&t,b,n); }
void ui_assets_init(void) {}
bool ui_assets_set(lv_obj_t *o,const char *p,unsigned a,unsigned b,unsigned c,unsigned d) { return false; }
bool ui_assets_set_blur(lv_obj_t *o,const char *p,unsigned a,unsigned b,unsigned c,unsigned d) { return false; }
void ui_style_refresh(void) {}
#include "../../components/ui/ui_visual.c"
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) { lv_display_flush_ready(d); }
int main(void)
{
 char date[100];struct tm t={.tm_year=126,.tm_mday=12,.tm_wday=4,.tm_mon=10};time_date_pt(&t,date,sizeof(date));assert(!strcmp(date,"Quinta-feira\n12 de novembro de 2026"));
 for(int m=0;m<12;m++){t.tm_mon=m;time_date_pt(&t,date,sizeof(date));assert(strstr(date," de ")&&strstr(date,"2026"));}
 t.tm_mon=2;t.tm_wday=2;time_date_pt(&t,date,sizeof(date));assert(strstr(date,"Terça-feira")&&strstr(date,"março"));
 char small[4];time_date_pt(&t,small,sizeof(small));assert(small[3]==0);t.tm_mon=12;time_date_pt(&t,date,sizeof(date));assert(strstr(date,"Ajuste"));
 lv_init(); lv_display_t *d=lv_display_create(1280,720); static uint32_t p[1280*720]; lv_display_set_buffers(d,p,NULL,sizeof(p),LV_DISPLAY_RENDER_MODE_DIRECT); lv_display_set_flush_cb(d,flush); ui_nav_init(lv_screen_active()); ui_visual_init(lv_screen_active());lv_display_add_event_cb(d,invalidated,LV_EVENT_INVALIDATE_AREA,NULL);
 for(int i=0;i<100;i++) {
  config.clock_style=i%4; config.clock_back=i%3; lv_tick_inc(1200); timer(NULL); assert(s_overlay&&s_dimmed&&light==20&&ui_nav_has_modal());
  assert(!strcmp(lv_label_get_text(s_date),"Quinta-feira\n29 de fevereiro de 2024"));
  lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(s_date,0),&glyph,'A',0));assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(s_date,0),&glyph,0xE7,0));
  lv_obj_update_layout(s_overlay);lv_area_t area;lv_obj_get_coords(s_date,&area);assert(area.x1>=0&&area.x2<1280&&area.y1>=0&&area.y2<720);
  if(config.clock_style!=3){lv_refr_now(d);invalidations=0;clock_update();assert(invalidations==0);}
  if(i==1 && getenv("TAB5_VISUAL_PREVIEW")) {
   lv_refr_now(d);FILE *f=fopen(getenv("TAB5_VISUAL_PREVIEW"),"wb");assert(f);fprintf(f,"P6\n1280 720\n255\n");
   for(unsigned k=0;k<1280*720;k++){unsigned char rgb[3]={p[k]>>16,p[k]>>8,p[k]};fwrite(rgb,1,3,f);}fclose(f);
  }
  lv_tick_inc(1000); timer(NULL); assert(s_off&&light==0);
  assert(ui_visual_activity()); assert(!s_overlay&&!s_off&&!s_dimmed&&light==80&&!ui_nav_has_modal());
  assert(!ui_visual_activity());
 }
 lv_obj_t *under=lv_button_create(lv_screen_active());lv_obj_set_size(under,1280,720);lv_obj_set_pos(under,0,0);lv_obj_add_event_cb(under,clicked,LV_EVENT_CLICKED,NULL);
 lv_indev_t *touch=lv_indev_create();lv_indev_set_type(touch,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(touch,read_touch);lv_indev_set_mode(touch,LV_INDEV_MODE_EVENT);
 for(unsigned style=0;style<4;style++)for(unsigned pos=0;pos<4;pos++) {
  config.clock_style=style;lv_tick_inc(1200);timer(NULL);assert(s_overlay);lv_obj_update_layout(s_overlay);
  touch_point=(lv_point_t){pos==0?30:640,pos==0?30:pos==1?280:pos==2?470:650};
  touch_down=true;lv_indev_read(touch);assert(!s_overlay&&!s_dimmed&&!s_off&&!ui_nav_has_modal());
  assert(!lv_timer_get_paused(lv_indev_get_read_timer(touch)));
  touch_down=false;lv_tick_inc(40);lv_timer_handler();assert(underneath_clicks==0);
 }
 lv_indev_delete(touch);lv_obj_delete(under);
 critical=true; lv_tick_inc(4000); timer(NULL); assert(!s_overlay&&!s_off); critical=false;
 lv_obj_clean(lv_screen_active()); lv_deinit(); puts("Visual: four clocks, three backgrounds, unchanged clock/date redraw suppression, 16 real pointer wake/release cases, dim/off, first-key wake, critical-operation guard and 100 overlay lifecycles passed (ASan/UBSan).");
}
