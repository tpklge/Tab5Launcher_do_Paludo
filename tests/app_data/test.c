#include "app_data.h"
#include "esp_partition.h"
#include "launcher_store.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/sha.h>
static esp_partition_t vfs={0xa10000,8192,"vfs",1,0x81},nvs={0x9000,24576,"nvs",1,2},apps[3]={{0x210000,0x400000,"app0",0,16},{0x610000,0x400000,"app1",0,17},{0x810000,0x200000,"app2",0,18}};
static uint8_t raw[8192],app_kv[24576];static int fail_write;static int64_t clock_value;
const esp_partition_t*esp_partition_find_first(int type,int subtype,const char*label){if(type==0)return subtype>=16&&subtype<=18?&apps[subtype-16]:NULL;return label&&!strcmp(label,"vfs")?&vfs:label&&!strcmp(label,"nvs")?&nvs:NULL;}
int esp_partition_get_sha256(const esp_partition_t*p,uint8_t*id){memset(id,p->subtype-15,32);return 0;}
int esp_partition_read(const esp_partition_t*p,size_t off,void*b,size_t n){assert((p==&vfs||p==&nvs)&&off+n<=p->size);memcpy(b,(p==&vfs?raw:app_kv)+off,n);return 0;}
int esp_partition_write(const esp_partition_t*p,size_t off,const void*b,size_t n){assert((p==&vfs||p==&nvs)&&off+n<=p->size);if(fail_write){fail_write=0;return ESP_FAIL;}memcpy((p==&vfs?raw:app_kv)+off,b,n);return 0;}
int esp_partition_erase_range(const esp_partition_t*p,size_t off,size_t n){assert((p==&vfs||p==&nvs)&&off+n<=p->size);memset((p==&vfs?raw:app_kv)+off,255,n);return 0;}
int64_t esp_timer_get_time(void){return clock_value+=1000;}
const char*esp_err_to_name(int e){return "test";}
/* CRC-backed SD record implementation is covered separately; this double keeps publication atomic. */
typedef struct{char key[48];void*data;size_t size;}record_t;static record_t records[64];
int launcher_store_init(void){mkdir(LAUNCHER_ROOT,0700);mkdir(LAUNCHER_PRIVATE,0700);return 0;}
int launcher_store_read(const char*k,void*b,size_t*n){for(unsigned i=0;i<64;i++)if(!strcmp(k,records[i].key)){if(*n<records[i].size)return ESP_ERR_INVALID_SIZE;*n=records[i].size;memcpy(b,records[i].data,*n);return 0;}return ESP_ERR_NOT_FOUND;}
int launcher_store_write(const char*k,const void*b,size_t n){unsigned i;for(i=0;i<64;i++)if(!records[i].key[0]||!strcmp(k,records[i].key))break;assert(i<64);strcpy(records[i].key,k);free(records[i].data);records[i].data=malloc(n);memcpy(records[i].data,b,n);records[i].size=n;return 0;}
static app_data_config_t config(unsigned app){app_data_config_t c={.count=2};memset(c.identity,app,32);c.regions[0]=(app_data_region_t){.label="vfs",.address=0xa10000,.size=8192};c.regions[1]=(app_data_region_t){.label="nvs",.address=0x9000,.size=24576,.nvs=true};return c;}
static uint32_t value(void){uint32_t v;memcpy(&v,app_kv,sizeof(v));return v==UINT32_MAX?0:v;}
static void data(uint8_t fill,uint32_t v){memset(raw,fill,sizeof(raw));memset(app_kv,fill,sizeof(app_kv));memcpy(app_kv,&v,sizeof(v));}
int main(void){char message[256];data(0xa1,11);app_data_config_t a=config(1),b=config(2);assert(!app_data_configure(&a,true,NULL,NULL,message,sizeof(message)));assert(!app_data_configure(&b,false,NULL,NULL,message,sizeof(message)));char bkey[48]="profile-";for(unsigned i=0;i<12;i++)strcat(bkey,"02");size_t old_b_size=0;void*old_b=NULL;for(unsigned i=0;i<64;i++)if(!strcmp(records[i].key,bkey)){old_b_size=records[i].size;old_b=malloc(old_b_size);memcpy(old_b,records[i].data,old_b_size);}assert(old_b);assert(!app_data_activate(0,NULL,NULL,message,sizeof(message)));data(0xa7,27);assert(!app_data_capture_return(NULL,NULL,message,sizeof(message)));assert(!app_data_activate(1,NULL,NULL,message,sizeof(message)));assert(raw[0]==255&&!value());data(0xb8,88);assert(!app_data_capture_return(NULL,NULL,message,sizeof(message)));assert(!app_data_activate(0,NULL,NULL,message,sizeof(message)));assert(raw[0]==0xa7&&value()==27);
 assert(!app_data_activate(2,NULL,NULL,message,sizeof(message)));data(0xc9,99);assert(!app_data_capture_return(NULL,NULL,message,sizeof(message)));assert(!app_data_activate(0,NULL,NULL,message,sizeof(message)));assert(raw[0]==0xa7&&value()==27);
 fail_write=1;assert(app_data_activate(1,NULL,NULL,message,sizeof(message))!=ESP_OK&&app_data_recovery_required());assert(app_data_activate(0,NULL,NULL,message,sizeof(message))!=ESP_OK);assert(!app_data_activate(1,NULL,NULL,message,sizeof(message))&&!app_data_recovery_required());assert(raw[0]==0xb8&&value()==88);
 fail_write=1;assert(app_data_activate(0,NULL,NULL,message,sizeof(message))!=ESP_OK);uint32_t stale=0;assert(!launcher_store_write("profile-transaction",&stale,sizeof(stale)));assert(app_data_recovery_required());assert(!app_data_capture_return(NULL,NULL,message,sizeof(message))&&!app_data_recovery_required());assert(raw[0]==0xa7&&value()==27);
 data(0xad,45);fail_write=1;assert(app_data_activate(1,NULL,NULL,message,sizeof(message))!=ESP_OK);assert(!app_data_recover(true,NULL,NULL,message,sizeof(message)));assert(raw[0]==0xad&&value()==45&&!app_data_recovery_required());
 fail_write=1;assert(app_data_activate(1,NULL,NULL,message,sizeof(message))!=ESP_OK);assert(!launcher_store_write(bkey,old_b,old_b_size));assert(!app_data_activate(1,NULL,NULL,message,sizeof(message)));assert(raw[0]==0xb8&&value()==88);free(old_b);
 for(unsigned i=0;i<64;i++)free(records[i].data);puts("App profiles: independent FAT/raw app-owned partitions, SD-only operational state, unconfigured app separation, failed switch and exact-target resume passed.");}
int launcher_file_write(const char*path,const void*data,size_t size){FILE*f=fopen(path,"wb");if(!f)return ESP_FAIL;bool ok=fwrite(data,1,size,f)==size;if(fclose(f))ok=false;return ok?ESP_OK:ESP_FAIL;}
