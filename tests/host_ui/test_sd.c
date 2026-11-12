#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui.h"
extern void ui_theme_apply(ui_theme_t,uint32_t);
void ui_style_refresh(void) { ui_theme_apply(UI_THEME_GRAPHITE,0xEB2638); }
esp_err_t installer_cancel_pending(const char *p) { return ESP_OK; }
bool installer_pending_source(char *p,size_t n) { return false; }
#include "installer.h"
#include "boot_mgr.h"
#include "input.h"
const char *esp_err_to_name(esp_err_t err) { return "simulated error"; }
bool board_sd_ready(void) { return true; }
esp_err_t board_sd_mount(void) { return ESP_OK; }
bool input_kbd_present(void) { return true; }
bool boot_mgr_return_supported(void) { return true; }
esp_err_t boot_mgr_launch_slot(int slot) { return ESP_FAIL; }
extern bool ui_sd_busy(void);
static int launched_slot=-1;
static boot_mgr_launch_cb_t pending_launch_callback;
void ui_post(void (*fn)(void*),void *user){fn(user);}
esp_err_t boot_mgr_launch_async(int slot,boot_mgr_launch_cb_t cb,void *user){assert(!ui_sd_busy());launched_slot=slot;pending_launch_callback=cb;return ESP_OK;}

esp_err_t installer_plan(const char *path,install_plan_t *p,char *e,size_t n) { return ESP_FAIL; }
esp_err_t installer_execute(const install_plan_t *p,installer_progress_fn cb,void *u,char *m,size_t n) { return ESP_FAIL; }
static bool fail_request_allocation;
static void *request_calloc(size_t n,size_t size) { return fail_request_allocation?NULL:calloc(n,size); }
esp_err_t flash_snapshot_validate(const char*p,flash_snapshot_t*s,char*m,size_t n){return ESP_FAIL;}
esp_err_t installer_full_restore(const flash_snapshot_t*s,installer_progress_fn cb,void*u,char*m,size_t n){return ESP_FAIL;}
void esp_restart(void){}
#define calloc request_calloc
#include "../../components/ui/ui_sd.c"
#undef calloc
static uint32_t pixels[1280*720];
static void flush(lv_display_t *d,const lv_area_t *area,uint8_t *data) { lv_display_flush_ready(d); }
static void tick(void) { lv_tick_inc(100); lv_timer_handler(); }
static request_t take_request(void) { request_t *p; assert(xQueueReceive(s_jobs,&p,0));request_t r=*p;free(p); atomic_store(&s_busy,false); return r; }
static void listing(unsigned generation) {
    result_t *r=calloc(1,sizeof(*r)); r->generation=generation; r->type=JOB_LIST;
    r->listing.count=12; r->listing.total=50;
    for(int i=0;i<12;i++) {
        snprintf(r->listing.entries[i].name,256,i<2?"Folder %d":"firmware %d.bin",i);
        r->listing.entries[i].directory=i<2; r->listing.entries[i].size=5500000;
    }
    xQueueSend(s_results,&r,0); tick();
}
static void shot(const char *file) {
    lv_refr_now(NULL); FILE *f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n1280 720\n255\n");
    for(int i=0;i<1280*720;i++) { unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]}; fwrite(rgb,1,3,f); } fclose(f);
}
int main(void) {
    lv_init(); lv_display_t *d=lv_display_create(1280,720);
    lv_display_set_color_format(d,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(d,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT); lv_display_set_flush_cb(d,flush);
    ui_nav_init(lv_screen_active());
    ui_sd_create(lv_screen_active()); request_t req=take_request(); assert(req.type==JOB_LIST);
    listing(s_generation); lv_obj_update_layout(s_page);
    fail_request_allocation=true;
    assert(!submit(JOB_LIST,s_cwd,NULL,NULL) && !atomic_load(&s_busy) && s_jobs->count==0);
    fail_request_allocation=false;
    assert(lv_obj_get_height(s_list)>450 && lv_obj_get_child_count(s_list)==12);
    lv_obj_send_event(lv_obj_get_child(s_list,3),LV_EVENT_CLICKED,NULL);
    assert(s_selected==3 && !lv_obj_has_state(s_analyze,LV_STATE_DISABLED));
    ui_nav_focus(s_analyze); assert(ui_nav_focused()==s_analyze);
    shot("sd-browser.ppm");
    name_dialog(JOB_MKDIR); assert(s_field && lv_obj_has_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN));
    const char *name="new folder";
    for(const char *p=name;*p;p++) { key_event_t key={.kind=KEY_KIND_CHAR,.ch=*p}; ui_sd_handle_key(&key); }
    key_event_t enter={.kind=KEY_KIND_ENTER}; ui_sd_handle_key(&enter);
    assert(!s_modal); req=take_request(); assert(req.type==JOB_MKDIR && strcmp(req.name,name)==0);
    delete_cb(NULL); assert(s_modal && !atomic_load(&s_busy));
    key_event_t esc={.kind=KEY_KIND_ESC}; ui_sd_handle_key(&esc); assert(!s_modal);
    /* Holding a folder selects it for rename/delete without navigating. */
    lv_obj_t *folder=lv_obj_get_child(s_list,0);
    lv_obj_send_event(folder,LV_EVENT_PRESSED,NULL); lv_obj_send_event(folder,LV_EVENT_LONG_PRESSED,NULL);
    lv_obj_send_event(folder,LV_EVENT_CLICKED,NULL); assert(s_selected==0 && strcmp(s_cwd,SD_FILES_ROOT)==0);
    memset(&s_plan,0,sizeof(s_plan)); strcpy(s_plan.path,SD_FILES_ROOT "/firmware.bin");
    strcpy(s_plan.target,"app0"); strcpy(s_plan.firmware.image.project,"Test App"); strcpy(s_plan.firmware.image.version,"1.0");
    s_plan.address=0x210000; s_plan.size=0x800000; s_plan.firmware.image.image_size=5500000;
    show_plan(); assert(lv_obj_has_state(s_install,LV_STATE_DISABLED));
    ui_nav_refresh(); ui_nav_focus(s_accept); ui_nav_send_key(LV_KEY_ENTER);
    assert(!lv_obj_has_state(s_install,LV_STATE_DISABLED)); shot("sd-install-plan.ppm");
    ui_nav_focus(s_install); assert(ui_nav_focused()==s_install); ui_nav_send_key(LV_KEY_ENTER); req=take_request(); assert(req.type==JOB_INSTALL && !s_modal);
    /* The launch must not enter the SD queue or mark itself busy. */
    s_plan.slot=1;launch_cb(NULL);assert(launched_slot==1&&!atomic_load(&s_busy)&&s_jobs->count==0&&pending_launch_callback);
    pending_launch_callback(ESP_ERR_TIMEOUT,NULL);assert(ui_nav_has_modal());ui_form_close();
    /* Late replies are ignored after the page is destroyed. */
    unsigned old=s_generation; lv_obj_clean(lv_screen_active()); ui_sd_create(lv_screen_active()); take_request();
    listing(old); assert(lv_obj_get_child_count(s_list)==0);
    listing(s_generation);
    for(int i=0;i<100;i++) { name_dialog(JOB_RENAME); ui_sd_handle_key(&esc); lv_obj_clean(lv_screen_active()); ui_sd_create(lv_screen_active()); take_request(); listing(s_generation); }
    lv_obj_clean(lv_screen_active()); free(s_jobs); free(s_results); free(s_progress);
    lv_display_delete(d); lv_deinit();
    puts("SD UI: list geometry, file/folder selection, physical entry, explicit destructive confirmation, install consent, stale replies and 100 lifecycle cycles passed (ASan/UBSan).");
}
void ui_navigate(ui_view_t view){}
