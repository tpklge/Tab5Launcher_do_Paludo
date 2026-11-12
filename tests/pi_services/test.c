#include "pi_services.h"
#include "sd_files.h"
#include "launcher_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static unsigned char app[4097],remote[16*1048576];
static unsigned stored;static int fail_read,corrupt;static int64_t tick;
int64_t esp_timer_get_time(void){return ++tick;}
esp_err_t pi_link_call(const char *service,const void *request,size_t length,void *body,size_t capacity,size_t *received,int *status,char *meta,size_t meta_size){
 *received=0;*status=200;if(meta_size)meta[0]=0;
 if(!strcmp(service,"apps")){const char *list="4097\t4000\t0\tTest\t1\tapp.bin\n";assert(capacity>=strlen(list));memcpy(body,list,strlen(list));*received=strlen(list);return 0;}
 if(!strcmp(service,"backup")){const char *p=memchr(request,'\n',length);assert(p);char head[200],name[100];unsigned off,total;size_t h=p-(const char*)request+1;memcpy(head,request,h);head[h]=0;assert(sscanf(head,"%99s %u %u",name,&off,&total)==3&&off==stored&&total==sizeof(remote));assert(length-h==32768&&off+32768<=sizeof(remote));memcpy(remote+off,p+1,32768);stored+=32768;return 0;}
 assert(!strcmp(service,"file"));char req[400],where[30],name[256];assert(length<sizeof(req));memcpy(req,request,length);req[length]=0;unsigned off,n;assert(sscanf(req,"%29s %u %u %255s",where,&off,&n,name)==4);unsigned size=!strcmp(where,"apps")?sizeof(app):sizeof(remote);unsigned char *data=!strcmp(where,"apps")?app:remote;
 snprintf(meta,meta_size,"size=%u\n",size);if(fail_read&&n)return ESP_FAIL;assert(off<=size&&n<=size-off&&n<=capacity);if(n)memcpy(body,data+off,n);if(corrupt&&n)((unsigned char*)body)[0]^=1;*received=n;return 0;
}
int main(void){assert(!mkdir(SD_FILES_ROOT,0755));assert(!mkdir(LAUNCHER_ROOT,0755));for(unsigned i=0;i<sizeof(app);i++)app[i]=(unsigned char)i;char list[1024],path[512];assert(!pi_services_list(false,list,sizeof(list))&&strstr(list,"app.bin"));assert(pi_services_fetch("apps","../escape",path,sizeof(path),NULL,NULL)==ESP_ERR_INVALID_ARG);assert(!pi_services_fetch("apps","app.bin",path,sizeof(path),NULL,NULL));FILE *f=fopen(path,"rb");assert(f);unsigned char data[4097];assert(fread(data,1,sizeof(data),f)==sizeof(data)&&!memcmp(data,app,sizeof(app)));fclose(f);fail_read=1;assert(pi_services_fetch("apps","bad.bin",path,sizeof(path),NULL,NULL)!=0);assert(fopen(path,"rb")==NULL);fail_read=0;
 char local[512];snprintf(local,sizeof(local),SD_FILES_ROOT "/snapshot.full");f=fopen(local,"wb");assert(f);memset(remote,0x5a,sizeof(remote));assert(fwrite(remote,1,sizeof(remote),f)==sizeof(remote));fclose(f);memset(remote,0,sizeof(remote));assert(!pi_services_backup(local,NULL,NULL)&&stored==sizeof(remote));for(unsigned i=0;i<sizeof(remote);i++)assert(remote[i]==0x5a);stored=0;corrupt=1;assert(pi_services_backup(local,NULL,NULL)==ESP_ERR_INVALID_CRC);puts("Pi services: shelf, bounded chunk receive/staging, failures and full backup/readback/hash mismatch passed (ASan/UBSan).");}
