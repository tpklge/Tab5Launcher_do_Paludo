#include "diagnostics.h"
#include "catalog.h"
#include "boot_mgr.h"
#include "backup.h"
#include "wifi_mgr.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>
void diagnostics_get_snapshot(diagnostics_snapshot_t *d){memset(d,0,sizeof(*d));}
esp_err_t diagnostics_capture(const char*p){return 0;}
bool installer_is_busy(void){return false;}
esp_err_t boot_mgr_launch_slot(int n){return ESP_FAIL;}
esp_err_t backup_slot(int s,installer_progress_fn p,void*u,char*m,size_t n){return ESP_FAIL;}
void wifi_retry(void){}
void catalog_source_get(char*p,size_t n){*p=0;}
int catalog_fetch(const char*u,catalog_list_t*l,char*m,size_t n){return 1;}
int launcher_web_publish(const char*s,const char*n){return 0;}
#include "../../components/usb_mgr/usb.c"
static void call(int n,const char **args){tab5(n,(char**)args);}
int main(void){
 const char *begin[]={"tab5","begin","test.bin","3","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"};
 call(5,begin);assert(s_upload && usb_transfer_active() && storage_io_busy());char file[256];strcpy(file,s_path);
 const char *wrong[]={"tab5","chunk","1","616263"};call(4,wrong);assert(!s_written);
 const char *invalid[]={"tab5","chunk","0","gg"};call(4,invalid);assert(!s_written);
 const char *large[]={"tab5","chunk","0","61626364"};call(4,large);assert(!s_written);
 const char *chunk[]={"tab5","chunk","0","616263"};call(4,chunk);assert(s_written==3);
 const char *commit[]={"tab5","commit"};call(2,commit);assert(!s_upload && !usb_transfer_active() && !storage_io_busy());
 FILE *f=fopen(file,"rb");assert(f);char bytes[4]={0};assert(fread(bytes,1,4,f)==3 && !strcmp(bytes,"abc"));fclose(f);
 call(5,begin);strcpy(file,s_path);const char *badchunk[]={"tab5","chunk","0","646566"};call(4,badchunk);call(2,commit);assert(!s_upload && access(file,F_OK));
 const char *badname[]={"tab5","begin","../escape.bin","3",begin[4]};call(5,badname);assert(!s_upload);
 usb_set_handoff_blocked(true);call(5,begin);assert(!s_upload);usb_set_handoff_blocked(false);
 call(5,begin);strcpy(file,s_tmp);const char *abort[]={"tab5","abort"};call(2,abort);assert(!s_upload && !usb_transfer_active() && access(file,F_OK));
 puts("USB: SHA-256 commit, exact offsets, malformed/oversized chunks, traversal, blocked handoff and abort passed.");
}
