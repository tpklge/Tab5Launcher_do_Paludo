#include "ui.h"
#include "catalog.h"
#include "web.h"
#include "pi_link.h"
#include "backup.h"
#include "wifi_mgr.h"
#include "ui_nav.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define vQueueDelete free
static bool running;
const char *esp_err_to_name(int e){return "test";}
void ui_style_refresh(void){}
esp_err_t catalog_source_set(const char*u){return 0;}
void catalog_source_get(char*u,size_t n){snprintf(u,n,"https://example.com/catalog.json");}
bool catalog_https_url(const char*u){return !strncmp(u,"https://",8);}
esp_err_t catalog_fetch(const char*u,catalog_list_t*l,char*e,size_t n){return 0;}
esp_err_t catalog_fetch_progress(const char*u,catalog_list_t*l,char*e,size_t n,catalog_progress_fn cb,void*p){return 0;}
esp_err_t catalog_download(const catalog_item_t*i,char*p,size_t n,catalog_progress_fn cb,void*u){return 0;}
esp_err_t web_start(void){running=true;return 0;}
void web_stop(void){running=false;}
bool web_running(void){return running;}
void web_access_token(char*out,size_t n){snprintf(out,n,"12345678");}
void wifi_get_status(wifi_status_t *s){memset(s,0,sizeof(*s));strcpy(s->ip,"192.168.1.9");}
esp_err_t pi_link_init(void){return 0;}
esp_err_t pi_link_sync_time(void){return 0;}
esp_err_t pi_link_shutdown(void){return 0;}
void pi_link_status(pi_link_status_t *s){memset(s,0,sizeof(*s));}
esp_err_t backup_slot(int s,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
#include "partitions_mgr.h"
esp_err_t catalog_fetch_refresh(const char*u,catalog_list_t*l,char*e,size_t n,catalog_progress_fn cb,void*p){return 0;}
esp_err_t catalog_probe(catalog_item_t*i,catalog_progress_fn cb,void*p){return 0;}
void ui_sd_open_source(const char*p){}
void ui_sd_open_directory(const char*p){}
bool ui_assets_set(lv_obj_t*o,const char*p,unsigned a,unsigned b,unsigned c,unsigned d){return false;}
esp_err_t partitions_mgr_profile_plan(const uint32_t*s,unsigned c,uint8_t*h){memset(h,0,32);return 0;}
esp_err_t installer_repartition(const uint32_t*s,unsigned c,const uint8_t*h,installer_progress_fn cb,void*u,char*m,size_t n){return -1;}
void esp_restart(void){}
#include "catalog_tracking.h"
esp_err_t catalog_download_from(const catalog_item_t*i,const char*s,char*p,size_t n,catalog_progress_fn cb,void*u){return 0;}
esp_err_t flash_migrate_plan(const uint32_t*s,unsigned c,uint8_t*h,char*m,size_t n){return 0;}
esp_err_t installer_full_backup(flash_snapshot_t*s,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
esp_err_t flash_snapshot_validate(const char*p,flash_snapshot_t*s,char*m,size_t n){return 0;}
esp_err_t installer_full_restore(const flash_snapshot_t*s,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
esp_err_t installer_resume_flash(installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
bool flash_operation_pending(void){return false;}bool app_data_recovery_required(void){return false;}
esp_err_t installer_recover_profile(bool b,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
esp_err_t catalog_check_updates(catalog_updates_t*l,catalog_progress_fn cb,void*u){return 0;}
esp_err_t catalog_download_updates(const catalog_updates_t*l,catalog_progress_fn cb,void*u,char*m,size_t n){return 0;}
esp_err_t catalog_link_slot(int s,const catalog_item_t*i,const char*u){return 0;}
#include "../../components/ui/ui_services.c"
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*b){lv_display_flush_ready(d);}
int main(void){
 lv_init();lv_display_t*d=lv_display_create(1280,720);static uint8_t pixels[1280*20*4];lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(d,flush);ui_nav_init(lv_screen_active());
 lv_obj_t *p=ui_catalog_create(lv_screen_active());assert(p && s_timer && s_catalog);assert(ui_services_busy());assert(!submit(0,0,NULL));
 job_t job;assert(xQueueReceive(s_jobs,&job,0) && job.kind==0);
 s_catalog->count=1;strcpy(s_catalog->items[0].name,"Demo");strcpy(s_catalog->items[0].version,"1");s_catalog->items[0].count=1;
 result_t r={.kind=0};strcpy(r.message,"Pronto");xQueueSend(s_results,&r,0);poll(NULL);assert(!ui_services_busy() && lv_obj_get_child_count(s_items)==1);
 s_show_updates=true;s_updates->count=2;for(unsigned i=0;i<2;i++){snprintf(s_updates->rows[i].item.name,96,"Update%u",i);s_updates->rows[i].item.count=1;strcpy(s_updates->rows[i].item.version,"2");strcpy(s_updates->rows[i].previous,"1");s_updates->rows[i].changed=true;}show_items();assert(lv_obj_get_child_count(s_items)==2);lv_obj_send_event(lv_obj_get_child(s_items,1),LV_EVENT_CLICKED,NULL);assert(s_detail_pending&&s_detail_index==1);assert(xQueueReceive(s_jobs,&job,0)&&job.kind==9);r.kind=9;xQueueSend(s_results,&r,0);poll(NULL);assert(s_detail_panel && lv_obj_get_child_count(s_detail_panel)>0);s_show_updates=false;
 lv_obj_delete(p);assert(!s_page && s_timer);assert(submit(1,0,NULL));xQueueReceive(s_jobs,&job,0);r.kind=1;xQueueSend(s_results,&r,0);poll(NULL);assert(!ui_services_busy());
 for(unsigned i=0;i<100;i++){p=create(lv_screen_active(),i%4);assert(p);poll(NULL);lv_obj_delete(p);assert(!s_page);}
 while(xQueueReceive(s_jobs,&job,0)){}atomic_store(&s_busy,false);
 p=ui_partitions_create(lv_screen_active());assert(p&&s_layout&&xQueueReceive(s_jobs,&job,0)&&job.kind==21);
 r=(result_t){.kind=21,.layout={.launcher_size=0x1f0000,.reserved={0x100000,0x200000},.minimum={0x100000}}};xQueueSend(s_results,&r,0);poll(NULL);
 lv_obj_update_layout(p);for(unsigned k=0;k<4;k++){lv_area_t row,label;lv_obj_get_coords(lv_obj_get_parent(s_layout->values[k]),&row);lv_obj_get_coords(s_layout->values[k],&label);assert(label.x1>=row.x1 && label.x2<=row.x2);}
 assert(s_layout->ready&&lv_obj_get_child_count(s_layout->bar)==6&&!lv_obj_has_state(s_layout->apply,LV_STATE_DISABLED));
 assert(!strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(s_layout->bar,-1),0)),"Sem particao"));
 lv_slider_set_value(s_layout->sliders[1],48,LV_ANIM_OFF);lv_obj_send_event(s_layout->sliders[1],LV_EVENT_VALUE_CHANGED,NULL);assert(s_layout->sizes[1]==0x300000&&s_layout->dirty);poll(NULL);assert(!s_layout->dirty && !strcmp(lv_label_get_text(s_layout->values[1]),"3.00 MiB"));
 lv_slider_set_value(s_layout->sliders[0],0,LV_ANIM_OFF);assert(lv_slider_get_value(s_layout->sliders[0])==16);
 manual_plan(NULL);assert(xQueueReceive(s_jobs,&job,0)&&job.kind==22&&job.sizes[1]==0x300000);
 r=(result_t){.kind=22};strcpy(r.message,"Mapa verificado");xQueueSend(s_results,&r,0);poll(NULL);assert(ui_form_panel());ui_nav_send_key(LV_KEY_ESC);assert(!ui_nav_has_modal());
 lv_obj_delete(p);assert(!s_layout);
 p=ui_web_create(lv_screen_active());running=true;poll(NULL);assert(strstr(lv_label_get_text(s_status),"192.168.1.9"));lv_obj_delete(p);
 lv_timer_delete(s_timer);free(s_catalog);free(s_origins);free(s_updates);free(s_jobs);free(s_results);lv_deinit();puts("Services UI: catalog results, job serialization, closed-view results, Web token and 100 view lifecycles passed.");
}

esp_err_t catalog_fetch_sources(catalog_list_t*l,char (*o)[512],bool r,char*m,size_t n,catalog_progress_fn cb,void*u){return 0;}
esp_err_t flash_allocate_plan(uint32_t r,flash_allocation_t*a,char*m,size_t n){memset(a,0,sizeof(*a));a->launcher_size=0x1f0000;a->free_bytes=0x800000;return 0;}
esp_err_t installer_compact(const uint8_t*h,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}

esp_err_t flash_layout_plan(const uint32_t*s,flash_allocation_t*a,char*m,size_t n){return flash_allocate_plan(0,a,m,n);}
esp_err_t installer_layout_apply(const uint32_t*s,const uint8_t*h,installer_progress_fn cb,void*u,char*m,size_t n){return 0;}
