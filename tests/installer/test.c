#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/sha.h>
#include "installer.h"
#include "launcher_store.h"
#include "boot_mgr.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "launcher_state.h"
#ifndef TEST_SLOT_SIZE
#define TEST_SLOT_SIZE 131072
#endif
static unsigned char slot[TEST_SLOT_SIZE],original[TEST_SLOT_SIZE];
static esp_partition_t app={0x210000,sizeof(slot),"app0"},factory={0x20000,0x1f0000,"launcher"};
static bool running_app,secure,capability=true,journal_fails,journal_read_fails,end_fails,mutate;
static unsigned written,stored,begins,aborts,phase;
static char mutation_path[SD_PATH_MAX];
static esp_partition_t data_part={0xa10000,8192,"vfs",1,0x81};
static unsigned char data_bytes[8192];
void board_shutdown(void) {}
void esp_restart(void) { assert(!"Unexpected restart in app/data installer regression"); }
esp_err_t partitions_mgr_big_plan(uint8_t digest[32]) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t partitions_mgr_apply_big(const uint8_t digest[32],installer_progress_fn cb,void *u,char *m,unsigned n) { return ESP_FAIL; }
int launcher_state_get_blob(launcher_state_handle_t h,const char *k,void *v,size_t *n) { return ESP_FAIL; }
int launcher_state_set_blob(launcher_state_handle_t h,const char *k,const void *v,size_t n) { return ESP_OK; }
int launcher_state_erase_key(launcher_state_handle_t h,const char *k) { return ESP_OK; }
int esp_partition_write(const esp_partition_t *p,size_t off,const void *b,size_t n) { assert(p==&data_part && off+n<=sizeof(data_bytes)); memcpy(data_bytes+off,b,n); return ESP_OK; }
int test_lock=1;
static int default_slot=0,stopped_slot=0;
static unsigned erases;
int boot_mgr_get_default(void) { return default_slot; }
esp_err_t boot_mgr_set_default(int slot) { default_slot=slot; return ESP_OK; }
int boot_mgr_stopped_slot(void) { return stopped_slot; }
void boot_mgr_clear_stopped(void) { stopped_slot=-1; }
int esp_partition_erase_range(const esp_partition_t *p,size_t off,size_t n) {
    if(p==&data_part) { assert(off+n<=sizeof(data_bytes)); memset(data_bytes+off,255,n); return ESP_OK; }
    assert(p==&app && off+n<=sizeof(slot) && off%4096==0 && n%4096==0);
    erases++; memset(slot+off,0xff,n); stored=0; return ESP_OK;
}
const char *esp_err_to_name(int e) { return e?"simulated error":"ESP_OK"; }
int esp_flash_get_size(void *c,uint32_t *n) { *n=0x1000000; return ESP_OK; }
bool esp_flash_encryption_enabled(void) { return secure; }
bool esp_secure_boot_enabled(void) { return false; }
bool boot_mgr_return_supported(void) { return capability; }
int64_t esp_timer_get_time(void) { static int64_t t=100; return ++t; }
const esp_partition_t *esp_ota_get_running_partition(void) { return running_app?&app:&factory; }
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label) { return type==1 && label && !strcmp(label,"vfs")?&data_part:type==0&&(subtype==16||(subtype==255&&label&&!strcmp(label,"app0")))?&app:NULL; }
int esp_ota_get_partition_description(const esp_partition_t *p,esp_app_desc_t *d) {
    if(!stored) return ESP_FAIL; memcpy(d->project_name,slot+32+48,32); return ESP_OK;
}
int esp_partition_read(const esp_partition_t *p,size_t off,void *out,size_t n) { if(p==&data_part) { assert(off+n<=sizeof(data_bytes)); memcpy(out,data_bytes+off,n); return ESP_OK; } assert(off+n<=sizeof(slot)); memcpy(out,slot+off,n); return ESP_OK; }
int esp_partition_get_sha256(const esp_partition_t *p,uint8_t digest[32]) { if(!stored) return ESP_FAIL; SHA256(slot,stored,digest); return ESP_OK; }
int esp_ota_begin(const esp_partition_t *p,size_t size,esp_ota_handle_t *h) {
    assert(p==&app && size<=sizeof(slot) && !running_app); begins++; memset(slot,0xff,sizeof(slot)); written=stored=0; *h=1; return ESP_OK;
}
int esp_ota_write(esp_ota_handle_t h,const void *data,size_t n) {
    assert(h==1 && n<=4096 && written+n<=sizeof(slot)); memcpy(slot+written,data,n); written+=n;
    if(mutate) { mutate=false; FILE *f=fopen(mutation_path,"r+b"); assert(f); fseek(f,6000,SEEK_SET); fputc(0xff,f); fclose(f); }
    return ESP_OK;
}
static bool flash_read(void *ctx,uint32_t off,void *out,size_t n) { if(off+n>written) return false; memcpy(out,slot+off,n); return true; }
static bool flash_hash(void *ctx,uint32_t size,const uint8_t digest[32]) { uint8_t d[32]; SHA256(slot,size,d); return memcmp(d,digest,32)==0; }
int esp_ota_end(esp_ota_handle_t h) {
    firmware_image_t info; char error[160];
    unsigned off=24;
    for(unsigned i=0;i<slot[1];i++) { uint32_t n; memcpy(&n,slot+off+4,4); off+=8+n; }
    unsigned length=((off+16)&~15u)+(slot[23]?32:0);
    if(end_fails || length>written || !firmware_parse(NULL,flash_read,flash_hash,length,103,&info,error,sizeof(error))) return ESP_FAIL;
    stored=length; return ESP_OK;
}
int esp_ota_abort(esp_ota_handle_t h) { aborts++; return ESP_OK; }
int esp_image_verify(int mode,const esp_partition_pos_t *pos,esp_image_metadata_t *meta) {
    assert(pos->offset==app.address); meta->image_len=stored; return stored?ESP_OK:ESP_FAIL;
}
int launcher_state_open(const char *ns,int mode,launcher_state_handle_t *h) { *h=(launcher_state_handle_t)1; return (journal_fails&&mode==LAUNCHER_STATE_READWRITE)||(journal_read_fails&&mode==LAUNCHER_STATE_READONLY)?ESP_FAIL:ESP_OK; }
void launcher_state_close(launcher_state_handle_t h) {}
int launcher_state_set_str(launcher_state_handle_t h,const char *k,const char *v) { return ESP_OK; }
int launcher_state_set_u8(launcher_state_handle_t h,const char *k,uint8_t v) { phase=v; return ESP_OK; }
int launcher_state_get_u8(launcher_state_handle_t h,const char *k,uint8_t *v) { *v=phase; return ESP_OK; }
int launcher_state_commit(launcher_state_handle_t h) { return ESP_OK; }
static int cancel_at=-1;
static bool progress_cb(unsigned percent,const char *stage,void *user) { return cancel_at<0 || percent<(unsigned)cancel_at; }
static void copy_file(const char *in,const char *out) { FILE *a=fopen(in,"rb"),*b=fopen(out,"wb"); assert(a&&b); char buf[1024]; size_t n; while((n=fread(buf,1,sizeof(buf),a))) assert(fwrite(buf,1,n,b)==n); fclose(a); fclose(b); }
#include "flash_snapshot.h"
static bool force_layout;
esp_err_t flash_allocate_plan(uint32_t required,flash_allocation_t*out,char*m,size_t n){
 if(required>sizeof(slot))return ESP_ERR_INVALID_SIZE;
 for(unsigned i=0;i<sizeof(slot);i++)if(slot[i]!=255)return ESP_ERR_NO_MEM;
 memset(out,0,sizeof(*out));out->slot=0;out->address=app.address;out->size=app.size;out->changed=force_layout;return ESP_OK;
}
static void empty_app(void){memset(slot,255,sizeof(slot));stored=0;}
int main(int argc,char **argv) {
 mkdir(LAUNCHER_ROOT,0755); empty_app();assert(installer_init()==ESP_OK);
 phase=1;assert(installer_recovery_required());assert(installer_prepare_app_data(0,NULL,NULL,(char[80]){0},80)==ESP_ERR_INVALID_STATE);phase=2;assert(!installer_recovery_required());journal_read_fails=true;assert(installer_recovery_required());journal_read_fails=false;phase=99;assert(installer_recovery_required());phase=0;
 char path[SD_PATH_MAX],error[512];snprintf(path,sizeof(path),SD_FILES_ROOT "/test.bin");copy_file(argv[1],path);
 install_plan_t plan;assert(!installer_plan(path,&plan,error,sizeof(error))&&!plan.replaces_app);
 capability=false;assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==0);capability=true;
 cancel_at=20;assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==0);cancel_at=-1;
 journal_fails=true;assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==0);journal_fails=false;
 assert(!installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==1);
 assert(stored==plan.firmware.image.image_size);memcpy(original,slot,sizeof(slot));
 /* Occupied apps cannot be overwritten, even when only one mock partition exists. */
 copy_file(argv[2],path);assert(installer_plan(path,&plan,error,sizeof(error))==ESP_ERR_NO_MEM);
 assert(!memcmp(slot,original,sizeof(slot)));
 uint8_t digest[32];assert(!installer_slot_digest(0,digest));
 slot[100]^=1;assert(installer_uninstall_options(0,digest,false,0,progress_cb,NULL,error,sizeof(error)));memcpy(slot,original,sizeof(slot));
 running_app=true;assert(installer_uninstall_options(0,digest,false,0,progress_cb,NULL,error,sizeof(error)));running_app=false;
 /* Backup is optional: no backup directory is needed when unchecked. */
 assert(!installer_uninstall_options(0,digest,false,0,progress_cb,NULL,error,sizeof(error)));
 assert(access(LAUNCHER_BACKUPS,F_OK));
 assert(!installer_plan(path,&plan,error,sizeof(error)));assert(!installer_execute(&plan,progress_cb,NULL,error,sizeof(error)));
 assert(!installer_slot_digest(0,digest));memcpy(original,slot,sizeof(slot));
 assert(!installer_uninstall_options(0,digest,true,0,progress_cb,NULL,error,sizeof(error)));
 DIR *dir=opendir(LAUNCHER_BACKUPS);assert(dir);struct dirent *ent;bool have=false;
 while((ent=readdir(dir)))if(strstr(ent->d_name,".raw")&&!strstr(ent->d_name,".json")){char b[1024];snprintf(b,sizeof(b),LAUNCHER_BACKUPS "/%s",ent->d_name);FILE*f=fopen(b,"rb");assert(f);unsigned char data[sizeof(slot)];assert(fread(data,1,sizeof(data),f)==sizeof(data));fclose(f);assert(!memcmp(data,original,sizeof(slot)));have=true;}
 closedir(dir);assert(have);
 /* Raw backup staging must include its checked JSON sidecar. */
 dir=opendir(LAUNCHER_BACKUPS);assert(dir);char raw[SD_PATH_MAX]={0};
 while((ent=readdir(dir)))if(strstr(ent->d_name,".raw")&&!strstr(ent->d_name,".json")){snprintf(raw,sizeof(raw),LAUNCHER_BACKUPS "/%s",ent->d_name);break;}closedir(dir);assert(*raw);
 force_layout=true;assert(!installer_plan(raw,&plan,error,sizeof(error))&&plan.needs_layout);
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))==ESP_ERR_NOT_SUPPORTED);force_layout=false;

 /* Source mutation after review must fail before writing. */
 assert(!installer_plan(path,&plan,error,sizeof(error)));copy_file(argv[1],path);unsigned before=begins;
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==before);
 assert(!installer_plan(path,&plan,error,sizeof(error)));cancel_at=40;
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&aborts);cancel_at=-1;
 empty_app();copy_file(argv[1],path);assert(!installer_plan(path,&plan,error,sizeof(error)));
 snprintf(mutation_path,sizeof(mutation_path),"%s",path);mutate=true;before=aborts;
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&aborts>before);
 empty_app();copy_file(argv[1],path);assert(!installer_plan(path,&plan,error,sizeof(error)));end_fails=true;
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error)));end_fails=false;empty_app();
 running_app=true;assert(installer_plan(path,&plan,error,sizeof(error)));running_app=false;
 secure=true;assert(installer_plan(path,&plan,error,sizeof(error)));secure=false;
 snprintf(path,sizeof(path),SD_FILES_ROOT "/package.json");assert(!installer_plan(path,&plan,error,sizeof(error))&&plan.firmware.data_count==1);
 memset(data_bytes,0x91,sizeof(data_bytes));before=begins;
 assert(installer_execute(&plan,progress_cb,NULL,error,sizeof(error))&&begins==before);
 assert(!installer_plan(path,&plan,error,sizeof(error)));assert(!installer_execute(&plan,progress_cb,NULL,error,sizeof(error)));
 FILE*f=fopen(SD_FILES_ROOT "/data.bin","rb");assert(f);unsigned char data[8192];assert(fread(data,1,sizeof(data),f)==sizeof(data));fclose(f);assert(!memcmp(data,data_bytes,sizeof(data)));
 puts("Installer: no implicit overwrite, optional verified backup, source mutation, cancellation, journal/SDK failures, data partition and security gates passed.");return 0;
}

esp_partition_iterator_t esp_partition_find(int t,int s,const char*l){return &data_part;}
const esp_partition_t *esp_partition_get(esp_partition_iterator_t p){return p;}
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t p){return NULL;}
esp_err_t partitions_mgr_profile_plan(const uint32_t*s,unsigned c,uint8_t*h){return ESP_ERR_NOT_SUPPORTED;}
esp_err_t partitions_mgr_apply_profile(const uint8_t*h,const uint32_t*s,unsigned c,installer_progress_fn cb,void*u,char*m,unsigned n){return ESP_ERR_NOT_SUPPORTED;}
int launcher_store_read(const char*k,void*p,size_t*n){return ESP_ERR_NOT_FOUND;}
int launcher_store_write(const char*k,const void*p,size_t n){return ESP_OK;}
