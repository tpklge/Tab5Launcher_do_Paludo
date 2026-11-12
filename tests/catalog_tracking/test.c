#include "catalog_tracking.h"
#include "esp_partition.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
static esp_partition_t apps[3]={{.subtype=16},{.subtype=17},{.subtype=18}};
const esp_partition_t*esp_partition_find_first(int t,int sub,const char*l){return sub>=16&&sub<=18?&apps[sub-16]:NULL;}
int esp_partition_get_sha256(const esp_partition_t*p,uint8_t*id){memset(id,p->subtype,32);return 0;}
int esp_ota_get_partition_description(const esp_partition_t*p,esp_app_desc_t*d){strcpy(d->version,"1.0");return 0;}
bool catalog_https_url(const char*s){return !strncmp(s,"https://",8);}
typedef struct{char key[48];void*data;size_t size;}record_t;static record_t records[16];
int launcher_store_write(const char*k,const void*b,size_t n){unsigned i;for(i=0;i<16;i++)if(!records[i].key[0]||!strcmp(records[i].key,k))break;assert(i<16);strcpy(records[i].key,k);free(records[i].data);records[i].data=malloc(n);memcpy(records[i].data,b,n);records[i].size=n;return 0;}
int launcher_store_read(const char*k,void*b,size_t*n){for(unsigned i=0;i<16;i++)if(!strcmp(records[i].key,k)){assert(*n>=records[i].size);*n=records[i].size;memcpy(b,records[i].data,*n);return 0;}return ESP_ERR_NOT_FOUND;}
static int queries,downloads,fail_network;static catalog_item_t newest[2];
int catalog_fetch_latest(const char*s,catalog_list_t*l,char*m,size_t n,catalog_progress_fn cb,void*u){queries++;assert(!strcmp(s,"https://example.com/catalog"));if(fail_network)return ESP_FAIL;l->count=2;l->items[0]=newest[0];l->items[1]=newest[1];return 0;}
int catalog_download_from(const catalog_item_t*i,const char*s,char*p,size_t n,catalog_progress_fn cb,void*u){downloads++;snprintf(p,n,"/sdcard/downloads/%s.bin",i->name);return 0;}
int main(void){for(unsigned i=0;i<2;i++){snprintf(newest[i].id,48,"app%u",i);snprintf(newest[i].name,96,"App%u",i);strcpy(newest[i].version,"2.0");newest[i].count=1;strcpy(newest[i].parts[0].url,"https://example.com/app.bin");strcpy(newest[i].parts[0].file,"app.bin");}
 assert(!catalog_link_slot(0,&newest[0],"https://example.com/catalog"));catalog_item_t old=newest[1];strcpy(old.version,"1.0");assert(!catalog_record_download("/sdcard/old.bin",&old,"https://example.com/catalog"));uint8_t id[32];memset(id,17,32);assert(!catalog_alias_download("/sdcard/old.bin","/sdcard/launcher/incoming/staged.bin"));assert(!catalog_track_install("/sdcard/launcher/incoming/staged.bin",id));catalog_updates_t*list=calloc(1,sizeof(*list));assert(!catalog_check_updates(list,NULL,NULL));assert(queries==1&&list->count==2&&list->untracked==1&&list->rows[0].changed&&list->rows[1].changed);char message[256];assert(!catalog_download_updates(list,NULL,NULL,message,sizeof(message))&&downloads==2);fail_network=1;assert(!catalog_check_updates(list,NULL,NULL)&&list->failed==1&&!list->count);free(list);for(unsigned i=0;i<16;i++)free(records[i].data);puts("OTA tracking: manual/installed origins, grouped network check, missing origins, differing versions, batch download and failed-source reporting passed.");}
