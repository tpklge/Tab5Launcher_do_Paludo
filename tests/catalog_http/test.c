#include "catalog.h"
#include "esp_http_client.h"
#include "nvs.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include "launcher_store.h"
static bool cache_enabled;
static const char *body="{\"schema\":1,\"items\":[]}";
static size_t cursor;static int code=200,headers_fail,open_fail,opens,cleanups;static char url[512];
int64_t esp_timer_get_time(void){return 100000;}
int64_t esp_http_client_get_content_length(void*h){return strlen(body);}
const char*esp_err_to_name(int e){return "ESP_FAIL";}
void*esp_http_client_init(const esp_http_client_config_t*c){assert(c->buffer_size>=16384 && c->disable_auto_redirect && c->crt_bundle_attach);strcpy(url,c->url);cursor=0;return (void*)1;}
int esp_http_client_open(void*h,int n){opens++;return open_fail?-1:0;}
int64_t esp_http_client_fetch_headers(void*h){return headers_fail?-1:(int64_t)strlen(body);}
int esp_http_client_get_status_code(void*h){return code;}
int esp_http_client_set_redirection(void*h){strcpy(url,"http://example.com/insecure");return 0;}
int esp_http_client_get_url(void*h,char*out,int n){snprintf(out,n,"%s",url);return 0;}
int esp_http_client_close(void*h){return 0;}
int esp_http_client_cleanup(void*h){cleanups++;return 0;}
int esp_http_client_read(void*h,char*out,int n){assert(n<=4096);size_t remaining=strlen(body)-cursor;if(remaining>5)remaining=5;if(remaining>(size_t)n)remaining=n;memcpy(out,body+cursor,remaining);cursor+=remaining;return remaining;}
bool esp_http_client_is_complete_data_received(void*h){return cursor==strlen(body);}
int nvs_open(const char*a,int b,nvs_handle_t*c){return -1;}void nvs_close(nvs_handle_t h){}int nvs_get_str(nvs_handle_t h,const char*k,char*p,size_t*n){return -1;}
int launcher_store_read(const char*k,void*p,size_t*n){return ESP_ERR_NOT_FOUND;}int launcher_store_write(const char*k,const void*p,size_t n){return 0;}
static int progress_calls;
static bool cancel_progress(unsigned n,const char*s,void*u){return ++progress_calls<3;}
int main(void){
 catalog_list_t*l=calloc(1,sizeof(*l));char error[256];const char *source="https://example.com/catalog.json";
 assert(catalog_fetch(source,l,error,sizeof(error))==ESP_OK && !l->count && cleanups==1);
 assert(catalog_fetch_progress(source,l,error,sizeof(error),cancel_progress,NULL)==ESP_ERR_INVALID_STATE && strstr(error,"cancelada"));
 code=403;assert(catalog_fetch(source,l,error,sizeof(error))!=ESP_OK && strstr(error,"403"));
 code=200;headers_fail=1;assert(catalog_fetch(source,l,error,sizeof(error))!=ESP_OK && strstr(error,"cabecalhos") && !strstr(error,"200"));headers_fail=0;
 open_fail=1;assert(catalog_fetch(source,l,error,sizeof(error))!=ESP_OK && strstr(error,"HTTPS") && !strstr(error,"200"));open_fail=0;
 body="<!doctype html><title>GitHub page</title>";assert(catalog_fetch(source,l,error,sizeof(error))!=ESP_OK && strstr(error,"HTML"));
 code=302;int before=opens;assert(catalog_fetch(source,l,error,sizeof(error))!=ESP_OK && strstr(error,"HTTPS") && opens==before+1);
 cache_enabled=true;mkdir(LAUNCHER_ROOT,0755);memset(l,0,sizeof(*l));l->count=1;strcpy(l->items[0].name,"Ação");strcpy(l->items[0].id,"demo");strcpy(l->items[0].version,"1");l->items[0].count=1;strcpy(l->items[0].parts[0].url,"https://example.com/app.bin");strcpy(l->items[0].parts[0].file,"app.bin");assert(catalog_cache_write(source,l)==ESP_OK);l->count=0;int64_t age;assert(catalog_cache_read(source,l,&age)==ESP_OK&&l->count==1&&!strcmp(l->items[0].name,"Ação"));before=opens;assert(catalog_fetch(source,l,error,sizeof(error))==ESP_OK&&opens==before&&strstr(error,"Cache"));
 code=200;body="{\"schema\":1,\"items\":[]}";assert(catalog_fetch_refresh(source,l,error,sizeof(error),NULL,NULL)==ESP_OK&&opens>before&&!l->count);
 DIR*dir=opendir(LAUNCHER_ROOT "/cache");assert(dir);struct dirent*de;while((de=readdir(dir)))if(strstr(de->d_name,"catalog-")){char path[512];snprintf(path,sizeof(path),LAUNCHER_ROOT "/cache/%s",de->d_name);FILE*f=fopen(path,"r+b");assert(f);fputc(0,f);fclose(f);}closedir(dir);assert(catalog_cache_read(source,l,&age)!=ESP_OK&&!l->count);
 free(l);puts("OTA HTTP: large headers budget, fragmented HTTP 200 JSON, real 403, incomplete headers, TLS failure, HTML response and HTTPS downgrade passed.");
}

int launcher_store_init(void){return cache_enabled?ESP_OK:ESP_FAIL;}

int catalog_transfer(const catalog_part_t*p,const char*s,catalog_progress_fn cb,void*u){return ESP_FAIL;}
int catalog_record_download(const char*p,const catalog_item_t*i,const char*s){return ESP_OK;}
