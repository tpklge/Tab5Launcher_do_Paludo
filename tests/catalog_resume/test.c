#include "catalog_transfer.h"
#include "esp_http_client.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
static unsigned char payload[1024];static size_t position,range_offset;static int opens,fail_after=-1,code;static bool ignore_range,wrong_range,no_etag,bad_hash,weak_etag;static esp_http_client_config_t cfg;
typedef struct{char key[48];unsigned char data[1024];size_t size;}disk_t;static disk_t disk[16];
int launcher_store_read(const char*k,void*b,size_t*n){for(unsigned i=0;i<16;i++)if(!strcmp(disk[i].key,k)){assert(*n>=disk[i].size);*n=disk[i].size;memcpy(b,disk[i].data,*n);return 0;}return ESP_ERR_NOT_FOUND;}
int launcher_store_write(const char*k,const void*b,size_t n){assert(n<1024);unsigned i;for(i=0;i<16;i++)if(!disk[i].key[0]||!strcmp(disk[i].key,k))break;assert(i<16);strcpy(disk[i].key,k);disk[i].size=n;memcpy(disk[i].data,b,n);return 0;}
bool catalog_https_url(const char*s){return !strncmp(s,"https://",8);}
void*esp_http_client_init(const esp_http_client_config_t*c){cfg=*c;range_offset=0;return (void*)1;}
int esp_http_client_set_header(void*h,const char*k,const char*v){if(!strcmp(k,"Range")){assert(!strncmp(v,"bytes=",6));range_offset=strtoul(v+6,NULL,10);}if(!strcmp(k,"If-Range"))assert(!strcmp(v,"\"etag-v1\""));return 0;}
int esp_http_client_open(void*h,int n){opens++;code=range_offset&&!ignore_range?206:200;position=code==206?range_offset:0;return 0;}
int64_t esp_http_client_fetch_headers(void*h){esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=cfg.user_data,.header_key="ETag",.header_value="\"etag-v1\""};if(weak_etag)event.header_value="W/\"etag-v1\"";if(!no_etag)cfg.event_handler(&event);char range[96];if(code==206){snprintf(range,sizeof(range),"bytes %lu-1023/1024",(unsigned long)range_offset+(wrong_range?1:0));event.header_key="Content-Range";event.header_value=range;cfg.event_handler(&event);}return 1024-position;}
int esp_http_client_get_status_code(void*h){return code;}
int64_t esp_http_client_get_content_length(void*h){return code==206?1024-range_offset:1024;}
int esp_http_client_set_redirection(void*h){return -1;}int esp_http_client_get_url(void*h,char*b,int n){snprintf(b,n,"https://example.com/part.bin");return 0;}int esp_http_client_close(void*h){return 0;}int esp_http_client_cleanup(void*h){return 0;}
int esp_http_client_read(void*h,char*out,int n){if(fail_after>=0&&position>=(size_t)fail_after)return -1;if(position==1024)return 0;if(n>100)n=100;if((size_t)n>1024-position)n=1024-position;memcpy(out,payload+position,n);position+=n;return n;}
bool esp_http_client_is_complete_data_received(void*h){return position==1024;}
static void verify(const char*path){FILE*f=fopen(path,"rb");assert(f);unsigned char actual[1025];assert(fread(actual,1,sizeof(actual),f)==1024&&!memcmp(actual,payload,1024));fclose(f);}
int main(int argc,char**argv){assert(argc==2);for(unsigned i=0;i<1024;i++)payload[i]=i;catalog_part_t part={.url="https://example.com/part.bin",.file="part.bin",.size=1024};uint8_t digest[32];mbedtls_sha256(payload,1024,digest,0);for(unsigned i=0;i<32;i++)snprintf(part.sha256+2*i,3,"%02x",digest[i]);char path[512],partial[528];
 for(unsigned scenario=0;scenario<6;scenario++){snprintf(path,sizeof(path),"%s/test%u.bin",argv[1],scenario);snprintf(partial,sizeof(partial),"%s.part",path);ignore_range=wrong_range=no_etag=weak_etag=false;if(scenario>=3)part.sha256[0]=0;no_etag=scenario==4;weak_etag=scenario==5;fail_after=300;assert(catalog_transfer(&part,path,NULL,NULL)!=ESP_OK);struct stat st;assert(!stat(partial,&st)&&st.st_size==300);fail_after=-1;ignore_range=scenario==1;wrong_range=scenario==2;if(wrong_range){assert(catalog_transfer(&part,path,NULL,NULL)==ESP_ERR_INVALID_CRC);assert(!stat(partial,&st)&&st.st_size==300);wrong_range=false;}assert(!catalog_transfer(&part,path,NULL,NULL));verify(path);if(scenario>=4)assert(range_offset==0);else if(scenario!=1)assert(range_offset==300);int previous=opens;assert(!catalog_transfer(&part,path,NULL,NULL)&&opens==previous);}
 puts("OTA resume: interrupted prefix, valid Range, ignored Range restart, wrong Content-Range rejection, ETag/no-hash and completed integrity checks passed.");
}
