#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "app_registry.h"
extern void ui_theme_apply(ui_theme_t,uint32_t);
void ui_style_refresh(void) { ui_theme_apply(UI_THEME_GRAPHITE,0xEB2638); }
bool ui_assets_set(lv_obj_t *o,const char *p,unsigned a,unsigned b,unsigned c,unsigned d) { return false; }
bool app_registry_get(const uint8_t *i,app_metadata_t *m) { memset(m,0,sizeof(*m)); return false; }
esp_err_t app_registry_set(const uint8_t *i,const app_metadata_t *m) { return ESP_OK; }
int boot_mgr_stopped_slot(void) { return -1; }
void boot_mgr_clear_stopped(void) {}
int boot_mgr_get_default(void) { return 0; }
esp_err_t boot_mgr_set_default(int slot) { return ESP_OK; }
#include "ui_nav.h"
#include "installer.h"
#include "boot_mgr.h"
unsigned installer_data_options(int s,installer_data_option_t *o){o[0]=(installer_data_option_t){.label="vfs",.size=8192,.associated=true};o[1]=(installer_data_option_t){.label="nvs",.size=24576,.associated=false};return 2;}
esp_err_t installer_uninstall_data(int s,const uint8_t*d,uint32_t mask,installer_progress_fn cb,void*u,char*m,size_t n){return installer_uninstall(s,d,cb,u,m,n);}

#include "esp_ota_ops.h"
static int launched=-1;
const char *esp_err_to_name(int e) { return "error"; }
void ui_post(void (*fn)(void *),void *arg) { fn(arg); }
void ui_navigate(ui_view_t v) {}
esp_err_t boot_mgr_launch_async(int slot,boot_mgr_launch_cb_t cb,void *user) { launched=slot; return ESP_OK; }
static esp_partition_t parts[]={{0x210000,0x400000,"app0"},{0x610000,0x400000,"app1"}};
static esp_partition_t data_parts[]={{0xa10000,8192,"vfs"},{0x9000,24576,"nvs"}};
const esp_partition_t *esp_partition_find_first(int t,int s,const char *l) {if(t==1&&l)return !strcmp(l,"vfs")?&data_parts[0]:!strcmp(l,"nvs")?&data_parts[1]:NULL;return s==16?&parts[0]:s==17?&parts[1]:NULL;}
esp_err_t esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n) { memset(out,0xe9,n); return ESP_OK; }
esp_err_t esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d) { memset(d,0,sizeof(*d)); strcpy(d->project_name,p==parts?"First":"Second"); strcpy(d->version,"1.0"); return ESP_OK; }
esp_err_t installer_slot_digest(int slot,uint8_t d[32]) { memset(d,slot+1,32); return ESP_OK; }
esp_err_t installer_uninstall(int slot,const uint8_t d[32],installer_progress_fn cb,void *u,char *m,size_t n) { return ESP_FAIL; }
bool app_data_get(const uint8_t*i,app_data_config_t*c){return false;}
esp_err_t installer_profile_configure(const app_data_config_t*c,bool capture,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
#include "../../components/ui/ui_home.c"
esp_err_t esp_partition_get_sha256(const esp_partition_t *p,uint8_t d[32]) { memset(d,p==parts?1:2,32); return ESP_OK; }
static uint32_t pixels[1280*720];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p) { lv_display_flush_ready(d); }
static job_t take(void) { job_t j; assert(xQueueReceive(s_jobs,&j,0)); atomic_store(&s_busy,false); return j; }
static void complete(unsigned gen) { result_t *r=calloc(1,sizeof(*r)); r->generation=gen; inventory(r); xQueueSend(s_results,&r,0); poll(NULL); }
static lv_obj_t *find_button(lv_obj_t *parent,const char *text) {
 for(unsigned i=0;i<lv_obj_get_child_count(parent);i++) {
  lv_obj_t *o=lv_obj_get_child(parent,i);
  if(lv_obj_check_type(o,&lv_button_class) && lv_obj_get_child_count(o) && strcmp(lv_label_get_text(lv_obj_get_child(o,0)),text)==0) return o;
  lv_obj_t *found=find_button(o,text); if(found) return found;
 } return NULL;
}
int main(void) {
 lv_init(); lv_display_t *d=lv_display_create(1280,720); lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
 lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT); lv_display_set_flush_cb(d,flush);
 ui_nav_init(lv_screen_active()); ui_home_create(lv_screen_active()); job_t j=take(); complete(j.generation);
 assert(s_model.count==2 && s_model.apps[0].used && s_model.apps[1].used);
 assert(lv_obj_get_child_count(s_carousel)==2 && strstr(lv_label_get_text(s_count),"2 app(s)"));
 lv_obj_update_layout(s_page); assert(lv_obj_get_height(s_cards[1])==450);
 lv_obj_t *start=find_button(s_cards[1],"INICIAR APP"),*remove=find_button(s_cards[1],"Desinstalar"); assert(start && remove);
 ui_nav_focus(start); ui_nav_send_key(LV_KEY_ENTER); assert(launched==1);
 ui_nav_focus(remove); ui_nav_send_key(LV_KEY_ENTER); assert(s_modal && !atomic_load(&s_busy));
 ui_nav_send_key(LV_KEY_ESC); assert(!s_modal && !atomic_load(&s_busy));
 ui_nav_send_key(LV_KEY_ENTER); assert(s_modal);
 lv_obj_t *confirm=find_button(s_modal,"Desinstalar"); ui_nav_focus(confirm); ui_nav_send_key(LV_KEY_ENTER);
 assert(!s_modal && ui_progress_visible()); j=take(); assert(j.remove && j.slot==1 && j.digest[0]==2 && j.data_mask==1);
 result_t *r=calloc(1,sizeof(*r)); r->generation=s_generation; inventory(r); r->apps[1].used=false;
 strcpy(r->apps[1].name,"Slot livre"); xQueueSend(s_results,&r,0); poll(NULL);
 assert(!ui_progress_visible() && strstr(lv_label_get_text(s_count),"1 app(s)"));
 assert(find_button(s_cards[1],"INSTALAR DO SD") && !find_button(s_cards[1],"Desinstalar"));
 lv_obj_t*profile_button=find_button(s_cards[0],"Dados do app");assert(profile_button);lv_obj_send_event(profile_button,LV_EVENT_CLICKED,NULL);assert(s_modal&&s_profile_checks[0]&&s_profile_checks[1]);lv_obj_add_state(s_profile_checks[0],LV_STATE_CHECKED);lv_obj_add_state(s_profile_checks[1],LV_STATE_CHECKED);profile_save(NULL);j=take();assert(j.profile&&j.capture&&j.profile_config.count==2&&j.profile_config.identity[0]==1);complete(s_generation);
 unsigned old=s_generation; lv_obj_clean(lv_screen_active()); ui_home_create(lv_screen_active()); take(); complete(old);
 assert(lv_obj_get_child_count(s_carousel)==0); complete(s_generation); assert(lv_obj_get_child_count(s_carousel)==2);
 lv_refr_now(NULL); FILE *f=fopen("home-two-apps.ppm","wb"); assert(f); fprintf(f,"P6\n1280 720\n255\n");
 for(int i=0;i<1280*720;i++) { unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]}; fwrite(rgb,1,3,f); } fclose(f);
 lv_obj_clean(lv_screen_active()); free(s_jobs); free(s_results); free(s_progress); lv_deinit();
 puts("HOME: two apps/cards, native keyboard launch/removal confirmation, safe Escape, freed slot and stale replies passed (ASan/UBSan).");
}

esp_err_t installer_uninstall_options(int s,const uint8_t*d,bool b,uint32_t mask,installer_progress_fn cb,void*u,char*m,size_t n){return installer_uninstall(s,d,cb,u,m,n);}
