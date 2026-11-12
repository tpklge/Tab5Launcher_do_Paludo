#include "catalog.h"
#include "cJSON.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 catalog_list_t *list=calloc(1,sizeof(*list));assert(list);
 const char *valid="{\"schema\":1,\"items\":[{\"id\":\"demo\",\"name\":\"Teste\",\"version\":\"1.0\",\"files\":[{\"file\":\"app.bin\",\"url\":\"https://example.com/app.bin\",\"size\":65536,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}]}]}";
 assert(catalog_parse(valid,list)==ESP_OK && list->count==1 && list->items[0].count==1);
 assert(!catalog_https_url("http://example.com/a"));assert(!catalog_https_url("https://user:secret@example.com/a"));assert(!catalog_https_url("https://example.com/a\r\nHeader:x"));assert(!catalog_https_url("https:///a"));
 const char *bad[]={"{}","[]","{\"schema\":1,\"schema\":1,\"items\":[]}","{\"schema\":2,\"items\":[]}","{\"schema\":1,\"items\":[]}junk"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(catalog_parse(bad[i],list)!=ESP_OK && !list->count);
 const char *keys[]={"file","url","size","sha256"};
 const char *values[]={"../app.bin","http://example.com/a","0","bad"};
 for(unsigned i=0;i<4;i++) {cJSON *root=cJSON_Parse(valid),*item=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"items"),0),*file=cJSON_GetArrayItem(cJSON_GetObjectItem(item,"files"),0);cJSON_ReplaceItemInObject(file,keys[i],i==2?cJSON_CreateNumber(0):cJSON_CreateString(values[i]));char *text=cJSON_PrintUnformatted(root);assert(catalog_parse(text,list)!=ESP_OK);free(text);cJSON_Delete(root);}
 char deep[256];memset(deep,'[',100);memset(deep+100,']',100);deep[200]=0;
 assert(catalog_parse(deep,list)!=ESP_OK);
 free(list);puts("Catalog strict parser and URL regressions passed.");
}
