#include "catalog.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
 catalog_list_t*l=calloc(1,sizeof(*l));assert(l);
 const char*source="https://github.com/0day1day/loadout-tab5/releases/download/v1.0.0/latest.json";
 const char*update="{\"version\":\"1.0.0\",\"url\":\"https://github.com/0day1day/loadout-tab5/releases/latest/download/loadout-tab5.bin\",\"notes\":\"LOADOUT 1.0.0\"}";
 assert(catalog_parse_any(update,source,l)==ESP_OK && l->count==1 && l->items[0].count==1 && !l->items[0].parts[0].sha256[0]);
 assert(!strcmp(l->items[0].parts[0].url,"https://github.com/0day1day/loadout-tab5/releases/download/v1.0.0/loadout-tab5.bin"));
 const char*bad="{\"version\":\"1\",\"url\":\"http://example.com/app.bin\"}";assert(catalog_parse_any(bad,source,l)!=ESP_OK);
 const char*feed="[{\"fid\":\"1\",\"name\":\"Tab5 Demo\",\"category\":\"tab5\",\"esp\":\"p4\",\"versions\":[{\"version\":\"old\",\"published_at\":\"2025-01-01\",\"file\":\"old.bin\"},{\"version\":\"new\",\"published_at\":\"2026-01-01\",\"file\":\"new.bin\"},{\"version\":\"unpublished\",\"published_at\":\"2027-01-01\",\"file\":\"bad.bin\",\"published\":false}]},{\"fid\":\"2\",\"name\":\"Other\",\"category\":\"cardputer\",\"esp\":\"s3\",\"versions\":[]}]";
 assert(catalog_parse_any(feed,"https://api.launcherhub.net/giveMeTheList",l)==ESP_OK && l->count==1 && !strcmp(l->items[0].version,"new"));
 if(argc>1){FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char*json=calloc(1,n+1);assert(fread(json,1,n,f)==n);fclose(f);assert(!catalog_parse_any(json,argv[2],l));printf("Real feed fixture: %u Tab5 firmware entries accepted\n",l->count);assert(l->count>0 && l->count<=128);free(json);}
 free(l);puts("Adapters: simple release, immutable tag, missing hash, HTTP rejection, device filter, latest published version passed.");
}
