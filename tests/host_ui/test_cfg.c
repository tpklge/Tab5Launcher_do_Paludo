#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
#include "storage.h"
#include "ui_nav.h"
#include "storage_guard.h"
static launcher_config_t config={.bright=80,.boot_time_s=3,.dim_s=60,.screen_off_s=300,.theme="graphite",.timezone="<-04>4"};
static bool critical,charge;
static unsigned off_calls,reloads,backlight,accent;
const launcher_config_t *storage_get_config(void) { return &config; }
esp_err_t storage_update(const launcher_config_t *c) { config=*c; return ESP_OK; }
esp_err_t boot_mgr_set_timeout(uint8_t seconds) { return ESP_OK; }
void board_backlight(uint8_t b) { backlight=b; }
bool board_charger_enabled(void) { return charge; }
esp_err_t board_charger_set(bool b) { charge=b; return ESP_OK; }
esp_err_t board_power_off(void) { off_calls++; return ESP_FAIL; }
bool installer_is_busy(void) { return critical; }
bool ui_services_power_safe(void){return true;}
bool ui_services_busy(void) {return false;}
bool ui_sd_busy(void) { return false; }
void wifi_forget_all(void) {}
esp_err_t time_set_manual(time_t utc) { return ESP_OK; }
void ui_set_theme(ui_theme_t t) {}
void ui_set_accent(uint32_t a) { accent=a; }
void ui_visual_reload(void) { reloads++; }
void ui_style_refresh(void) {}
void ui_navigate(ui_view_t view) {}
esp_err_t storage_export_json(const char*p){return ESP_OK;}
esp_err_t storage_import_json(const char*p){return ESP_OK;}
esp_err_t storage_import_theme(const char*p){return ESP_OK;}
const char *esp_err_to_name(int e) { return "test error"; }
#define vQueueDelete free
#include "../../components/ui/ui_cfg.c"
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *b) { lv_display_flush_ready(d); }
static request_t take(void) { request_t r; assert(xQueueReceive(s_jobs,&r,0)); return r; }
static lv_obj_t *find_class(lv_obj_t *root,const lv_obj_class_t *cls) { if(lv_obj_check_type(root,cls)) return root; for(unsigned i=0;i<lv_obj_get_child_count(root);i++) { lv_obj_t *o=find_class(lv_obj_get_child(root,i),cls); if(o) return o; } return NULL; }
int main(void)
{
 setenv("TZ","<-04>4",1); tzset(); lv_init(); lv_display_t *d=lv_display_create(1280,720); static uint32_t pixels[1280*720]; lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT); lv_display_set_flush_cb(d,flush);
 ui_nav_init(lv_screen_active()); lv_obj_t*host=lv_obj_create(lv_screen_active());lv_obj_set_pos(host,112,32);lv_obj_set_size(host,1168,688);lv_obj_set_style_pad_all(host,0,0);lv_obj_t *page=ui_cfg_create(host); ui_nav_refresh();
 extern void ui_theme_apply(ui_theme_t,unsigned);extern void ui_menu_fonts(lv_obj_t*);ui_theme_apply(UI_THEME_GRAPHITE,0xEF5368);ui_menu_fonts(page);
 lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(s_feedback,0),&glyph,0xE7,0));assert(lv_font_get_glyph_dsc(lv_obj_get_style_text_font(s_feedback,0),&glyph,0xE3,0));
 lv_obj_update_layout(page);lv_obj_t*body=lv_obj_get_child(page,1);assert(lv_obj_get_child_count(body)==3);
 lv_area_t a,b;lv_obj_get_coords(lv_obj_get_child(body,0),&a);lv_obj_get_coords(lv_obj_get_child(body,1),&b);assert(a.x2<b.x1 && lv_obj_get_width(lv_obj_get_child(body,0))>300);
 lv_obj_t *themes=find_class(page,&lv_dropdown_class);assert(themes&&lv_dropdown_get_option_count(themes)==9);
 for(unsigned i=0;i<9;i++){lv_dropdown_set_selected(themes,i);lv_obj_send_event(themes,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(s_edit.theme,launcher_theme_name(i)));ui_theme_apply((ui_theme_t)i,0xEF5368);(void)take();}
 lv_dropdown_set_selected(themes,0);lv_obj_send_event(themes,LV_EVENT_VALUE_CHANGED,NULL);(void)take();
 assert(lv_obj_has_flag(s_swatches[0],LV_OBJ_FLAG_USER_1));
 assert(lv_color_to_u32(lv_obj_get_style_bg_color(s_swatches[0],0))==lv_color_to_u32(lv_color_hex(colors[0])));
 lv_refr_now(NULL);FILE*image=fopen("cfg-three-columns.ppm","wb");assert(image);fprintf(image,"P6\n1280 720\n255\n");for(unsigned i=0;i<1280*720;i++){unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};fwrite(rgb,1,3,image);}fclose(image);

 for(unsigned i=0;i<144;i++) for(unsigned j=0;j<i;j++)assert(custom_color(i)!=custom_color(j));
 custom_picker(NULL);lv_obj_t *palette=ui_form_panel();assert(palette);lv_obj_t*grid=lv_obj_get_child(palette,lv_obj_get_child_count(palette)-1);assert(lv_obj_get_child_count(grid)==144);
 lv_obj_update_layout(palette);lv_area_t first,last;lv_obj_get_coords(lv_obj_get_child(grid,0),&first);lv_obj_get_coords(lv_obj_get_child(grid,11),&last);assert(first.y1==last.y1);lv_obj_get_coords(lv_obj_get_child(grid,12),&last);assert(last.y1>first.y1);ui_form_close();
 assert(!text_save("2024-02-30 00:00:00",(void*)2)); assert(!text_save("1999-01-01 00:00:00",(void*)2)); assert(!text_save("2024-02-29 00:00:00 garbage",(void*)2));
 assert(text_save("2024-02-29 00:00:00",(void*)2)); request_t r=take(); assert(r.op==TIME); struct tm utc; gmtime_r(&r.utc,&utc); assert(utc.tm_year==124&&utc.tm_mon==1&&utc.tm_mday==29&&utc.tm_hour==4);
 assert(!text_save("GG0000",NULL)); assert(text_save("FFFFFF",NULL)); r=take(); assert(r.op==SAVE && !strcmp(r.config.color,"FFFFFF") && accent==0xffffff);
 result_t result=apply_request(r); assert(!result.error&&!strcmp(config.color,"FFFFFF")); xQueueSend(s_results,&result,0); poll(NULL); assert(reloads==1);
 assert(!text_save("/sdcard/../secret",(void*)1)); assert(text_save("/sdcard/fundo.png",(void*)1)); r=take(); assert(!strcmp(r.config.wallpaper,"/sdcard/fundo.png")); apply_request(r);
 r=(request_t){.op=OFF};assert(storage_io_acquire());result=apply_request(r);assert(result.error!=ESP_OK&&off_calls==0);storage_io_release();assert(!storage_io_busy());
 r=(request_t){.op=OFF}; critical=true; result=apply_request(r); assert(result.error!=ESP_OK&&off_calls==0&&strstr(result.message,"Aguarde"));
 critical=false; apply_request(r); assert(off_calls==1);
 r=(request_t){.op=CHARGE,.enable=true}; assert(!apply_request(r).error&&charge);
 ui_confirm("Desligar aparelho","Deseja desligar?","Desligar",power_confirmed,NULL);assert(!find_class(lv_layer_top(),&lv_textarea_class));ui_nav_send_key(LV_KEY_ESC);assert(!ui_nav_has_modal()&&!s_jobs->count);
 ui_confirm("Desligar aparelho","Deseja desligar?","Desligar",power_confirmed,NULL);lv_obj_t*panel=ui_form_panel();lv_obj_send_event(lv_obj_get_child(panel,3),LV_EVENT_CLICKED,NULL);assert(!ui_nav_has_modal());r=take();assert(r.op==OFF);
 ui_datetime_open(date_confirmed,NULL);panel=ui_form_panel();lv_obj_t*date=find_class(panel,&lv_textarea_class);lv_obj_t*clock=NULL;for(unsigned i=0;i<lv_obj_get_child_count(panel);i++){lv_obj_t*o=lv_obj_get_child(panel,i);if(o!=date&&lv_obj_check_type(o,&lv_textarea_class))clock=o;}assert(clock);lv_textarea_set_text(date,"29022024");lv_textarea_set_text(clock,"123456");assert(!strcmp(lv_textarea_get_text(date),"29/02/2024")&&!strcmp(lv_textarea_get_text(clock),"12:34:56"));
 for(unsigned i=0;i<lv_obj_get_child_count(panel);i++){lv_obj_t*o=lv_obj_get_child(panel,i);if(lv_obj_has_flag(o,LV_OBJ_FLAG_USER_2)){lv_obj_send_event(o,LV_EVENT_CLICKED,NULL);break;}}
 r=take();assert(r.op==TIME);lv_timer_handler();ui_form_close();
 lv_obj_delete(page); free(s_jobs); free(s_results); lv_deinit(); puts("Settings: validated offline date/time, accent/wallpaper persistence, native form Enter/Escape and critical-operation power guard passed (ASan/UBSan).");
}
