#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app_registry.h"
#include "nvs.h"
static unsigned char blob[2048]; static size_t length; static int fail;
int nvs_open(const char *ns,int mode,nvs_handle_t *h) { *h=1; return 0; }
void nvs_close(nvs_handle_t h) {}
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) { if(!length) return -1; assert(*size>=length); memcpy(out,blob,length); *size=length; return 0; }
int nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) { if(fail) return fail; assert(size<=sizeof(blob)); memcpy(blob,in,size); length=size; return 0; }
int nvs_commit(nvs_handle_t h) { return fail; }
int launcher_store_read(const char*k,void*out,size_t*n){if(!length)return ESP_ERR_NOT_FOUND;if(*n<length)return ESP_FAIL;memcpy(out,blob,length);*n=length;return 0;}
int launcher_store_write(const char*k,const void*in,size_t n){if(fail)return fail;memcpy(blob,in,n);length=n;return 0;}
int main(void)
{
 uint8_t id[32]={1},other[32]={1}; other[31]=2; app_metadata_t data={.name="Nome personalizado",.icon="/sdcard/launcher/icons/icone.png"},out;
 assert(!app_registry_get(id,&out)); assert(app_registry_set(id,&data)==0); assert(app_registry_get(id,&out)&&!strcmp(out.name,data.name)&&!strcmp(out.icon,data.icon));
 assert(!app_registry_get(other,&out)); assert(app_registry_set(other,&data)!=0); /* same short key, different full identity */
 data.name[0]='\n'; assert(app_registry_set(id,&data)!=0); strcpy(data.name,"Seguro"); strcpy(data.icon,"/sdcard/../escape"); assert(app_registry_set(id,&data)!=0);
 strcpy(data.icon,"/sdcard/launcher/icons/good.png"); fail=77; assert(app_registry_set(id,&data)==77); fail=0;
 length--; assert(!app_registry_get(id,&out));
 puts("App registry: persistent image identity, rename/icon, key-collision protection, malformed paths/records and NVS failure passed (ASan/UBSan).");
}

int launcher_adopt_image(const char*s,const char*f,char*d,size_t n){snprintf(d,n,"%s",s);return 0;}
