#include "flash_snapshot.h"
#include "mbedtls/md5.h"
#include "boot_request.h"
#include "launcher_store.h"
#include <sys/stat.h>
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
const char *esp_err_to_name(int e){return "test";}
static bool protected=true,guard_read_error,journal_read_error,journal_invalid;
static uint8_t flash[0x1000000],original[0x1000000],record[16000];static size_t record_size;static int64_t tick;static uint32_t fail_at=UINT32_MAX;static unsigned writes,erases;
static esp_partition_t running={0x20000,0x1f0000,"launcher"};
const esp_partition_t*esp_ota_get_running_partition(void){return &running;}
bool esp_flash_encryption_enabled(void){return false;}bool esp_secure_boot_enabled(void){return false;}
int esp_flash_get_size(void*c,uint32_t*n){*n=sizeof(flash);return 0;}
int esp_flash_read(void*c,void*b,uint32_t a,uint32_t n){if(guard_read_error&&a==0x15000)return ESP_FAIL;assert(a+n<=sizeof(flash));memcpy(b,flash+a,n);return 0;}
int esp_flash_write(void*c,const void*b,uint32_t a,uint32_t n){if(a==0x8000)assert(!protected);assert(a+n<=sizeof(flash));writes++;if(a==fail_at){fail_at=UINT32_MAX;return ESP_FAIL;}memcpy(flash+a,b,n);return 0;}
int esp_flash_erase_region(void*c,uint32_t a,uint32_t n){if(a==0x8000)assert(!protected);assert(!(a&4095)&&!(n&4095)&&a+n<=sizeof(flash));erases++;memset(flash+a,255,n);return 0;}
int esp_image_verify(int mode,const esp_partition_pos_t*p,esp_image_metadata_t*m){m->image_len = p->offset == 0x20000 ? 0x1000 : 0x2000; return flash[p->offset]==0xe9?ESP_OK:ESP_FAIL;}
int64_t esp_timer_get_time(void){return tick+=1000;}
int launcher_store_read(const char*k,void*b,size_t*n){if(journal_read_error)return ESP_FAIL;if(journal_invalid){*n=1;return ESP_OK;}assert(!strcmp(k,"flash-operation"));if(!record_size)return ESP_ERR_NOT_FOUND;if(*n<record_size)return ESP_ERR_INVALID_SIZE;memcpy(b,record,record_size);*n=record_size;return 0;}
int launcher_store_write(const char*k,const void*b,size_t n){assert(!strcmp(k,"flash-operation")&&n<=sizeof(record));memcpy(record,b,n);record_size=n;return 0;}
static bool proceed(unsigned p,const char*s,void*u){return true;}
static bool cancel(unsigned p,const char*s,void*u){return false;}
static void init(const char*table){memset(flash,255,sizeof(flash));FILE*f=fopen(table,"rb");assert(f&&fread(flash+0x8000,1,3072,f)==3072);fclose(f);uint32_t cap=LAYOUT_CAP_MAGIC;memcpy(flash+BOOT_REQUEST_OFFSET+LAYOUT_CAP_OFFSET,&cap,4);memset(flash+0x20000,0x31,0x1000);flash[0x20000]=0xe9;memset(flash+0x210000,0x51,8192);flash[0x210000]=0xe9;memset(flash+0x610000,0x62,12288);flash[0x610000]=0xe9;memset(flash+0xa10000,0x75,8192);memcpy(original,flash,sizeof(flash));record_size=0;}
int main(int argc,char**argv){
 bool pending=false;guard_read_error=true;assert(flash_operation_status(&pending)!=ESP_OK&&pending&&flash_operation_pending());guard_read_error=false;
 journal_read_error=true;assert(flash_operation_status(&pending)!=ESP_OK&&pending&&flash_operation_pending());journal_read_error=false;
 journal_invalid=true;assert(flash_operation_status(&pending)!=ESP_OK&&pending&&flash_operation_pending());journal_invalid=false;
 uint32_t bad_magic=0x31504f00;memcpy(flash+0x15000,&bad_magic,4);assert(flash_operation_status(&pending)!=ESP_OK&&pending);memset(flash+0x15000,255,4);

mkdir(LAUNCHER_ROOT,0700);assert(argc==2);init(argv[1]);char message[512];uint32_t sizes[]={0x300000,0x300000,0x200000};uint8_t plan[32];assert(!flash_migrate_plan(sizes,3,plan,message,sizeof(message)));uint32_t tiny[]={0x100000,0x100000,0x600000};
 flash_snapshot_t snapshot;assert(!flash_snapshot_create(&snapshot,proceed,NULL,message,sizeof(message)));flash_snapshot_t checked;assert(!flash_snapshot_validate(snapshot.path,&checked,message,sizeof(message))&&!memcmp(snapshot.sha256,checked.sha256,32));unsigned old=erases;assert(flash_migrate_apply(sizes,3,plan,cancel,NULL,message,sizeof(message))!=ESP_OK&&erases==old);
 assert(!flash_migrate_apply(sizes,3,plan,proceed,NULL,message,sizeof(message)));assert(!flash_operation_pending());assert(!memcmp(flash+0x210000,original+0x210000,8192));assert(!memcmp(flash+0x510000,original+0x610000,12288));assert(!memcmp(flash+0xa10000,original+0xa10000,0x5f0000));assert(!flash_snapshot_restore(&snapshot,proceed,NULL,message,sizeof(message)));assert(!memcmp(flash+0x210000,original+0x210000,0xdf0000));
 init(argv[1]);assert(!flash_migrate_plan(sizes,3,plan,message,sizeof(message)));fail_at=0x510000;assert(flash_migrate_apply(sizes,3,plan,proceed,NULL,message,sizeof(message))!=ESP_OK&&flash_operation_pending());record[0]^=0x80;assert(!flash_operation_resume(proceed,NULL,message,sizeof(message))&&!flash_operation_pending());assert(!memcmp(flash+0x510000,original+0x610000,12288));
 init(argv[1]);assert(!flash_migrate_plan(sizes,3,plan,message,sizeof(message)));fail_at=0x8000;assert(flash_migrate_apply(sizes,3,plan,proceed,NULL,message,sizeof(message))!=ESP_OK&&flash_operation_pending());record[0]^=0x80;assert(!flash_operation_resume(proceed,NULL,message,sizeof(message))&&!flash_operation_pending());
 /* Protected image mismatch and source corruption must be rejected before writes. */
 flash[0x20000]^=1;old=erases;assert(flash_snapshot_restore(&snapshot,proceed,NULL,message,sizeof(message))!=ESP_OK&&erases==old);
 init(argv[1]);flash_allocation_t allocation;
 assert(!flash_allocate_plan(0x18001,&allocation,message,sizeof(message)));
 assert(allocation.launcher_size==0x10000 && allocation.slot==2 && allocation.size==0x20000);
 assert(allocation.address==0x50000 && allocation.changed);
 old=erases;uint8_t stale[32];memcpy(stale,allocation.digest,32);stale[0]^=1;
 assert(flash_allocate_apply(0x18001,stale,proceed,NULL,message,sizeof(message))!=ESP_OK && erases==old);
 assert(!flash_allocate_apply(0x18001,allocation.digest,proceed,NULL,message,sizeof(message)));
 assert(!memcmp(flash+0x30000,original+0x210000,8192));
 assert(!memcmp(flash+0x40000,original+0x610000,12288));
 assert(!memcmp(flash+0xa10000,original+0xa10000,0x5f0000));
 assert(!flash_operation_pending());
 assert(flash_allocate_plan(0xffffff,&allocation,message,sizeof(message))!=ESP_OK);
 init(argv[1]);uint32_t manual[16]={0x30000,0x20000,0x40000};
 assert(!flash_layout_plan(manual,&allocation,message,sizeof(message)));
 assert(allocation.minimum[0]==0x10000 && allocation.minimum[1]==0x10000 && allocation.reserved[0]==0x400000);
 uint8_t approved[32];memcpy(approved,allocation.digest,32);
 manual[0]=0;old=erases;assert(flash_layout_plan(manual,&allocation,message,sizeof(message))!=ESP_OK && erases==old);
 manual[0]=0x30000;manual[2]=0xa00000;assert(flash_layout_plan(manual,&allocation,message,sizeof(message))!=ESP_OK && erases==old);
 manual[2]=0x50000;assert(flash_layout_apply(manual,approved,proceed,NULL,message,sizeof(message))!=ESP_OK && erases==old);
 manual[2]=0x40000;assert(!flash_layout_apply(manual,approved,proceed,NULL,message,sizeof(message)));
 assert(!memcmp(flash+0x30000,original+0x210000,8192));assert(!memcmp(flash+0x60000,original+0x610000,12288));
 assert(!memcmp(flash+0xa10000,original+0xa10000,0x5f0000));assert(!flash_operation_pending());
 init(argv[1]);
 assert(!flash_allocate_plan(0x20000,&allocation,message,sizeof(message)));
 fail_at=0x30000;
 assert(flash_allocate_apply_options(0x20000,allocation.digest,false,proceed,NULL,message,sizeof(message))!=ESP_OK);
 assert(flash_operation_pending());
 uint32_t kind;memcpy(&kind,record+8,4);assert(kind==6);
 char work[512];memcpy(work,record+16,512);struct stat work_stat;
 assert(!stat(work,&work_stat)&&work_stat.st_size==4096+8192+12288);
 assert(!flash_operation_resume(proceed,NULL,message,sizeof(message)));
 assert(!flash_operation_pending() && access(work,F_OK));
 assert(!memcmp(flash+0x30000,original+0x210000,8192));
 assert(!memcmp(flash+0x40000,original+0x610000,12288));
 /* Build a minimal table without legacy data reservations. */
 init(argv[1]);uint8_t table[4096];memset(table,255,sizeof(table));unsigned at=0;
 for(unsigned j=0;j<3072&&flash[0x8000+j]==0xaa;j+=32){
   uint8_t *row=flash+0x8000+j;
   uint32_t address;memcpy(&address,row+4,4);
   if((row[2]!=0&&address>=0x20000)||(!row[2]&&row[3]>=16))continue;
   memcpy(table+at,row,32);at+=32;
 }
 table[at]=table[at+1]=0xeb;mbedtls_md5(table,at,table+at+16);
 memcpy(flash+0x8000,table,4096);
 memset(flash+0x30000,255,0xfc0000);
 assert(!flash_allocate_plan(0x900000,&allocation,message,sizeof(message)));
 assert(allocation.capacity==0xfc0000 && allocation.free_bytes>0x600000);
 assert(!flash_allocate_apply_options(0x900000,allocation.digest,false,proceed,NULL,message,sizeof(message)));
 memcpy(&kind,record+8,4);assert(kind==4 && !flash_operation_pending());
 /* Existing app bytes and system rows cannot be overlapped by new data. */
 old=erases;
 assert(flash_data_create("bad",0x82,8192,0x30000,false,proceed,NULL,message,sizeof(message))!=ESP_OK&&erases==old);
 assert(flash_data_create("launch_req",2,8192,0,false,proceed,NULL,message,sizeof(message))!=ESP_OK&&erases==old);
 assert(!flash_data_create("assets",0x82,0x20000,0xfd0000,false,proceed,NULL,message,sizeof(message)));
 assert(!flash_operation_pending());memcpy(&kind,record+8,4);assert(kind==5);
 assert(!flash_allocate_plan(0,&allocation,message,sizeof(message)));
 assert(allocation.capacity==0xfa0000);
 assert(flash_data_create("assets",0x82,0x20000,0,false,proceed,NULL,message,sizeof(message))!=ESP_OK);
 assert(protected);
 puts("Flash snapshots: preserved apps across moved slots, full compatible restore, cancellation, protected bytes and interrupted migration resume passed.");
}

int launcher_file_write(const char*p,const void*b,size_t n){mkdir(LAUNCHER_ROOT,0700);mkdir(LAUNCHER_PRIVATE,0700);FILE*f=fopen(p,"wb");if(!f)return ESP_FAIL;bool ok=fwrite(b,1,n,f)==n;if(fclose(f))ok=false;return ok?ESP_OK:ESP_FAIL;}

void *esp_flash_default_chip=(void*)1;

int esp_flash_set_dangerous_write_protection(void*c,bool p){assert(c==esp_flash_default_chip);protected=p;return 0;}
