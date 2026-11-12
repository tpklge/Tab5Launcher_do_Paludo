#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "partitions_mgr.h"
#include "launcher_store.h"
#include "boot_request.h"
#include "esp_ota_ops.h"
#include "sd_files.h"
static bool protected=true;
static unsigned char flash[0x1000000],original[0x1000000];
static unsigned erases;
static bool fail_table_write;
static esp_partition_t running={0x20000,0x1f0000,"launcher"};
const esp_partition_t *esp_ota_get_running_partition(void) { return &running; }
bool esp_flash_encryption_enabled(void) { return false; }
bool esp_secure_boot_enabled(void) { return false; }
int esp_flash_get_size(void *c,uint32_t *n) { *n=sizeof(flash); return 0; }
int esp_flash_read(void *c,void *out,uint32_t off,uint32_t n) { assert(off+n<=sizeof(flash)); memcpy(out,flash+off,n); return 0; }
int esp_flash_write(void *c,const void *in,uint32_t off,uint32_t n) { if(off==0x8000)assert(!protected); assert(off+n<=sizeof(flash)); if(fail_table_write&&off==0x8000) return ESP_FAIL; memcpy(flash+off,in,n); return 0; }
int esp_flash_erase_region(void *c,uint32_t off,uint32_t n) { if(off==0x8000)assert(!protected); assert(off%4096==0&&n%4096==0&&off+n<=sizeof(flash)); erases++; memset(flash+off,255,n); return 0; }
static bool cancel(unsigned n,const char *s,void *u) { return n<1; }
static bool proceed(unsigned n,const char *s,void *u) { return true; }
int main(int argc,char **argv)
{
 assert(argc==2); memset(flash,255,sizeof(flash)); FILE *f=fopen(argv[1],"rb"); assert(f); assert(fread(flash+0x8000,1,3072,f)==3072); fclose(f);
 uint32_t cap=LAYOUT_CAP_MAGIC; memcpy(flash+BOOT_REQUEST_OFFSET+LAYOUT_CAP_OFFSET,&cap,4);
 memset(flash+0x210000,0x91,32); memset(flash+0x610000,0x92,32); memset(flash+0xa10000,0x93,32); memcpy(original,flash,sizeof(flash));
 uint8_t sha[32]; char message[256]; assert(partitions_mgr_big_plan(sha)==ESP_OK);
 flash[0x8010]^=1; assert(partitions_mgr_apply_big(sha,proceed,NULL,message,sizeof(message))!=ESP_OK&&erases==0); flash[0x8010]^=1;
 assert(partitions_mgr_apply_big(sha,cancel,NULL,message,sizeof(message))!=ESP_OK&&erases==0&&!memcmp(flash,original,sizeof(flash)));
 assert(partitions_mgr_apply_big(sha,proceed,NULL,message,sizeof(message))==ESP_OK);
 for(unsigned i=0;i<sizeof(flash);i++) if((i<0x8000||i>=0x9000)&&(i<LAYOUT_MIRROR_OFFSET||i>=LAYOUT_TX_OFFSET+4096)) assert(flash[i]==original[i]);
 f=fopen(LAUNCHER_ROOT "/backups/layout-before.bin","rb"); assert(f); unsigned char buf[4096];
 for(unsigned off=0;off<sizeof(flash);off+=sizeof(buf)) { assert(fread(buf,1,sizeof(buf),f)==sizeof(buf)); assert(!memcmp(buf,original+off,sizeof(buf))); } fclose(f);
 layout_recovery_t tx; memcpy(&tx,flash+LAYOUT_TX_OFFSET,sizeof(tx)); assert(!layout_recovery_valid(&tx));
 assert(partitions_mgr_big_plan(sha)!=ESP_OK);
 memcpy(flash,original,sizeof(flash)); assert(partitions_mgr_big_plan(sha)==ESP_OK);
 assert(partitions_mgr_apply_big(sha,proceed,NULL,message,sizeof(message))!=ESP_OK); /* never overwrite the retained full backup */
 unlink(LAUNCHER_ROOT "/backups/layout-before.bin"); unlink(LAUNCHER_ROOT "/backups/layout-before.sha256");
 fail_table_write=true; assert(partitions_mgr_apply_big(sha,proceed,NULL,message,sizeof(message))!=ESP_OK);
 memcpy(&tx,flash+LAYOUT_TX_OFFSET,sizeof(tx)); assert(layout_recovery_valid(&tx));
 assert(boot_crc_bytes(flash+LAYOUT_MIRROR_OFFSET,4096)==tx.old_crc);
 fail_table_write=false;memcpy(flash,original,sizeof(flash));unlink(LAUNCHER_ROOT "/backups/layout-before.bin");unlink(LAUNCHER_ROOT "/backups/layout-before.sha256");
 uint32_t sizes[]={0x300000,0x300000,0x200000};assert(partitions_mgr_profile_plan(sizes,3,sha)==ESP_OK);assert(partitions_mgr_apply_profile(sha,sizes,3,proceed,NULL,message,sizeof(message))==ESP_OK);
 for(unsigned off=0x210000;off<0xa10000;off++)assert(flash[off]==255);
 assert(!memcmp(flash+0xa10000,original+0xa10000,sizeof(flash)-0xa10000));assert(!memcmp(flash+0x20000,original+0x20000,0x1f0000));
 assert(partitions_mgr_profile_plan(sizes,3,sha)==ESP_ERR_INVALID_STATE);
 puts("Layout transaction: full verified backup, cancellation, retained-copy protection, isolated writes and recoverable injected table failure passed (ASan/UBSan).");
}

void *esp_flash_default_chip=(void*)1;

int esp_flash_set_dangerous_write_protection(void*c,bool p){assert(c==esp_flash_default_chip);protected=p;return 0;}
