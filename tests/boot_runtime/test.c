#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "storage.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "launcher_state.h"
static launcher_config_t cfg;
static int8_t pending=-1,stopped=-1,default_slot=0;
static uint8_t failures,timeout=3;
static int reason,wakeup;
static bool valid_app=true,recovery_required,fail_launch_record;
static bool recovery_check(void){return recovery_required;}
static int cancelled_prepare;
static int prepare_race(int slot){recovery_required=true;return ESP_OK;}
static void cancel_prepare(void){cancelled_prepare++;}
static unsigned char request_bytes[4096];
static esp_partition_t app={0x210000,0x400000,"app0"},factory={0x20000,0x1f0000,"launcher"},request_part={0x12000,4096,"launch_req"};
static jmp_buf restart;
const launcher_config_t *storage_get_config(void) { return &cfg; }
esp_reset_reason_t esp_reset_reason(void) { return reason; }
int esp_sleep_get_wakeup_cause(void) { return wakeup; }
void esp_restart(void) { longjmp(restart,1); }
void board_shutdown(void) {}
const char *esp_err_to_name(int e) { return "error"; }
int launcher_state_open(const char *ns,int mode,launcher_state_handle_t *h) { *h=(launcher_state_handle_t)1; return 0; }
void launcher_state_close(launcher_state_handle_t h) {}
int launcher_state_commit(launcher_state_handle_t h) { return fail_launch_record?ESP_FAIL:ESP_OK; }
int launcher_state_get_i8(launcher_state_handle_t h,const char *key,int8_t *v) { *v=!strcmp(key,"pending_slot")?pending:!strcmp(key,"stopped_slot")?stopped:default_slot; return 0; }
int launcher_state_set_i8(launcher_state_handle_t h,const char *key,int8_t v) { if(!strcmp(key,"pending_slot")) pending=v; else if(!strcmp(key,"stopped_slot")) stopped=v; else default_slot=v; return 0; }
int launcher_state_get_u8(launcher_state_handle_t h,const char *key,uint8_t *v) { *v=!strncmp(key,"fail_",5)?failures:timeout; return 0; }
int launcher_state_set_u8(launcher_state_handle_t h,const char *key,uint8_t v) { if(!strncmp(key,"fail_",5)) failures=v; else timeout=v; return 0; }
int launcher_state_erase_key(launcher_state_handle_t h,const char *key) { if(!strcmp(key,"pending_slot")) pending=-1; if(!strcmp(key,"stopped_slot")) stopped=-1; if(!strncmp(key,"fail_",5)) failures=0; return 0; }
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label) { return type==1&&subtype==0x40?&request_part:type==0&&subtype==16?&app:type==0&&subtype==0?&factory:NULL; }
const esp_partition_t *esp_ota_get_running_partition(void) { return &factory; }
int esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d) { return valid_app?ESP_OK:ESP_FAIL; }
int esp_image_verify(int mode,const esp_partition_pos_t *pos,esp_image_metadata_t *m) { return valid_app?ESP_OK:ESP_FAIL; }
int esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n) { assert(p==&request_part&&off+n<=4096); memcpy(out,request_bytes+off,n); return 0; }
int esp_partition_write(const esp_partition_t *p,size_t off,const void *data,size_t n) { assert(p==&request_part&&off+n<=4096); memcpy(request_bytes+off,data,n); return 0; }
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n) { assert(p==&request_part&&off+n<=4096); memset(request_bytes+off,255,n); return 0; }
int esp_ota_set_boot_partition(const esp_partition_t *p) { return 0; }
int launcher_store_read(const char*k,void*out,size_t*n){*(int8_t*)out=default_slot;*n=1;return 0;}
int launcher_store_write(const char*k,const void*in,size_t n){default_slot=*(const int8_t*)in;return 0;}
int storage_update(const launcher_config_t*c){cfg=*c;return 0;}
#include "../../components/boot_mgr/boot_mgr.c"
static void reset_ram(int cause) { s_returned=false; s_stopped_slot=-1; reason=cause; uint32_t cap=BOOT_CAP_MAGIC; memcpy(request_bytes+BOOT_CAP_OFFSET,&cap,4); }
static void launch(void) { if(setjmp(restart)==0) { boot_mgr_launch_slot(0); assert(!"Launch did not restart"); } assert(pending==0); }
int main(void)
{
 boot_mgr_set_recovery_check(recovery_check);
 recovery_required=true;assert(boot_mgr_launch_slot(0)==ESP_ERR_INVALID_STATE);recovery_required=false;
 reset_ram(ESP_RST_POWERON);boot_mgr_set_prepare(prepare_race);boot_mgr_set_cancel_prepare(cancel_prepare);assert(boot_mgr_launch_slot(0)==ESP_ERR_INVALID_STATE&&pending==-1&&cancelled_prepare==1);recovery_required=false;boot_mgr_set_prepare(NULL);boot_mgr_set_cancel_prepare(NULL);
 reset_ram(ESP_RST_POWERON);
 uint32_t layout_cap=LAYOUT_CAP_MAGIC;memcpy(request_bytes+LAYOUT_CAP_OFFSET,&layout_cap,4);
 fail_launch_record=true;assert(boot_mgr_launch_slot(0)==ESP_FAIL);fail_launch_record=false;
 assert(boot_mgr_return_supported());memcpy(&layout_cap,request_bytes+LAYOUT_CAP_OFFSET,4);assert(layout_cap==LAYOUT_CAP_MAGIC);
 pending=-1;memset(request_bytes+LAYOUT_CAP_OFFSET,255,4);
 fail_launch_record=true;assert(boot_mgr_launch_slot(0)==ESP_FAIL);fail_launch_record=false;
 memcpy(&layout_cap,request_bytes+LAYOUT_CAP_OFFSET,4);assert(layout_cap==UINT32_MAX);
 pending=-1;
 reset_ram(ESP_RST_POWERON); assert(!boot_mgr_guard_take(NULL)); assert(boot_mgr_autoboot_slot()==0);
 for(int i=1;i<=3;i++) { launch(); reset_ram(ESP_RST_PANIC); boot_mgr_guard_state_t state; bool guarded=boot_mgr_guard_take(&state); assert(failures==i && guarded==(i==3)); assert(boot_mgr_autoboot_slot()==-1); if(guarded) { assert(state.slot==0 && state.restarts==3); boot_mgr_mark_stopped(0); } }
 reset_ram(ESP_RST_POWERON); assert(boot_mgr_autoboot_slot()==-1);
 boot_mgr_clear_stopped(); assert(boot_mgr_autoboot_slot()==0);
 launch(); reset_ram(ESP_RST_EXT); assert(!boot_mgr_guard_take(NULL) && failures==0 && boot_mgr_autoboot_slot()==-1);
 reset_ram(ESP_RST_POWERON); assert(!boot_mgr_guard_take(NULL)&&boot_mgr_autoboot_slot()==0);
 cfg.deep_sleep_start=true; wakeup=1; assert(boot_mgr_autoboot_slot()==-1); cfg.deep_sleep_start=false; assert(boot_mgr_autoboot_slot()==0);
 valid_app=false; assert(boot_mgr_autoboot_slot()==-1 && boot_mgr_launch_slot(0)!=ESP_OK && pending==-1);
 assert(boot_mgr_set_default(16)==ESP_ERR_INVALID_ARG); assert(boot_mgr_set_timeout(0)==ESP_ERR_INVALID_ARG);
 puts("Boot runtime: durable cross-app return, three crash failures, stopped/default, manual reset, deep-sleep rule and invalid image rejection passed (ASan/UBSan).");
}
