#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "firmware.h"
#include "sd_files.h"
static void check(const char *name,bool wanted,firmware_kind_t kind,unsigned data)
{
 char path[512],error[256]; snprintf(path,sizeof(path),SD_FILES_ROOT "/%s",name); firmware_info_t info;
 esp_err_t err=firmware_analyze_file(path,&info,error,sizeof(error));
 if((err==ESP_OK)!=wanted) fprintf(stderr,"%s: %d %s\n",name,err,error);
 assert((err==ESP_OK)==wanted);
 if(wanted) { assert(info.kind==kind && info.data_count==data); uint8_t sha[32]; if(kind!=FW_DATA_BACKUP) assert(firmware_hash_region(&info.app,sha)==ESP_OK && !memcmp(sha,info.sha256,32)); }
}
int main(void)
{
 check("app.bin",true,FW_APP_ONLY,0); check("merged.bin",true,FW_MERGED,2);
 check("merged-e9.bin",true,FW_MERGED,2); check("padded.bin",true,FW_APP_ONLY,0);
 check("package.json",true,FW_PACKAGE,1); check("backup.raw",true,FW_RAW_BACKUP,0);
 check("data.raw",true,FW_DATA_BACKUP,1);
 const char *bad[]={"wrongchip.json","traversal.json","wronghash.json","protected.json","duplicate.json","unknown.json","bootloader.json","partial.bin","ambiguous.bin","badtable.bin","broken.raw"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) check(bad[i],false,0,0);
 puts("Bundles: app, merged+data, multipart, app/data backup and malformed/ambiguous/protected inputs passed (ASan/UBSan).");
}
