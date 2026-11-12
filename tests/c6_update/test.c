#include "../../components/usb_mgr/c6_update.c"
#include <assert.h>
static int begin_calls,writes,ends,activates,paused,resumed,fail_phase;static size_t written;
esp_err_t installer_maintenance(esp_err_t (*fn)(void*),void *arg){return fn(arg);}
esp_err_t wifi_pause_wait(uint32_t t){paused++;return 0;}
void wifi_resume(void){resumed++;}
esp_err_t esp_hosted_slave_ota_begin(void){begin_calls++;return fail_phase==1?ESP_FAIL:0;}
esp_err_t esp_hosted_slave_ota_write(unsigned char *data,unsigned n){writes++;written+=n;return fail_phase==2?ESP_FAIL:0;}
esp_err_t esp_hosted_slave_ota_end(void){ends++;return fail_phase==3?ESP_FAIL:0;}
esp_err_t esp_hosted_slave_ota_activate(void){activates++;return fail_phase==4?ESP_FAIL:0;}
static void manifest(const char *board,const char *transport,const char *sha){FILE *f=fopen(LAUNCHER_ROOT "/c6/manifest.json","wb");assert(f);fprintf(f,"{\"board\":\"%s\",\"transport\":\"%s\",\"sha256\":\"%s\"}",board,transport,sha);fclose(f);}
int main(int argc,char **argv){assert(argc==2);const char *sha=argv[1];assert(usb_update_c6(sha)==ESP_ERR_NOT_FOUND&&!begin_calls);manifest("other","SDIO",sha);assert(usb_update_c6(sha)==ESP_ERR_INVALID_ARG&&!begin_calls);manifest("M5Stack Tab5","SPI",sha);assert(usb_update_c6(sha)==ESP_ERR_INVALID_ARG&&!begin_calls);manifest("M5Stack Tab5","SDIO",sha);assert(usb_update_c6("0000000000000000000000000000000000000000000000000000000000000000")==ESP_ERR_INVALID_ARG&&!begin_calls);
 assert(!usb_update_c6(sha)&&paused==1&&begin_calls==1&&writes>1&&ends==1&&activates==1&&written>1000000);
 for(fail_phase=1;fail_phase<=4;fail_phase++){begin_calls=writes=ends=activates=resumed=0;written=0;assert(usb_update_c6(sha)==ESP_FAIL&&begin_calls==1&&resumed==1);if(fail_phase<=2)assert(!ends&&!activates);if(fail_phase==3)assert(ends==1&&!activates);}
 puts("C6 update: real C6 image/hash, board/transport binding, phased RPC and failure ordering passed (ASan/UBSan).");}
