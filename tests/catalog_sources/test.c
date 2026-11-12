#include "catalog.h"
#include "launcher_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned calls;static int mode;
bool catalog_https_url(const char*u){return u&&!strncmp(u,"https://",8)&&strlen(u)<512;}
esp_err_t catalog_fetch_progress(const char*u,catalog_list_t*l,char*m,size_t n,catalog_progress_fn cb,void*v){calls++;if(mode==1)return ESP_ERR_INVALID_STATE;if(strstr(u,"bad"))return ESP_FAIL;l->count=128;for(unsigned i=0;i<128;i++)snprintf(l->items[i].name,96,"%s %u",u,i);return 0;}
esp_err_t catalog_fetch_refresh(const char*u,catalog_list_t*l,char*m,size_t n,catalog_progress_fn cb,void*v){return catalog_fetch_progress(u,l,m,n,cb,v);}
static void seed(const char*s){FILE*f=fopen(LAUNCHER_ROOT "/ota_sources.json","w");assert(f);fputs(s,f);fclose(f);}
int main(void){catalog_list_t*l=calloc(1,sizeof(*l));char(*o)[512]=calloc(128,512),m[512];assert(l&&o);
assert(catalog_fetch_sources(l,o,0,m,sizeof(m),NULL,NULL)==ESP_ERR_NOT_FOUND);
seed("{\"sources\":[{\"url\":\"https://one.test\"},{\"url\":\"https://two.test\"}]}");assert(!catalog_fetch_sources(l,o,0,m,sizeof(m),NULL,NULL)&&calls==2&&l->count==128);assert(!strcmp(o[0],"https://one.test")&&!strcmp(o[64],"https://two.test"));
mode=1;calls=0;assert(catalog_fetch_sources(l,o,1,m,sizeof(m),NULL,NULL)==ESP_ERR_INVALID_STATE&&calls==1);mode=0;
seed("{\"sources\":[{\"url\":\"https://one.test\"},{\"url\":\"http://two.test\"}]}");calls=0;assert(catalog_fetch_sources(l,o,0,m,sizeof(m),NULL,NULL)==ESP_ERR_INVALID_ARG&&calls==0);
seed("{\"sources\":[{\"url\":\"https://one.test\"},{\"url\":\"https://bad.test\"}]}");assert(!catalog_fetch_sources(l,o,0,m,sizeof(m),NULL,NULL)&&l->count==64);
free(l);free(o);puts("OTA manual sources: aggregation, source mapping, cache refresh, cancellation, partial failure and prevalidation passed.");}
