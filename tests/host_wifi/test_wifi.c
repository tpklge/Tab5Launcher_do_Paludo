#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_link.h"
#include "nvs.h"
#include "esp_wifi.h"

/* Old flash data must never be opened by the SD-only credential store. */
static unsigned char disk[4096], staged[4096];
static size_t disk_size, staged_size;
static int fail_set, fail_commit, legacy_count;
const char *esp_err_to_name(int err) { return err ? "simulated error" : "ESP_OK"; }
int nvs_open(const char *ns,int mode,nvs_handle_t *h) { assert(!"NVS access forbidden"); return ESP_FAIL; }
void nvs_close(nvs_handle_t h) { staged_size=0; }
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    if (!disk_size) return ESP_ERR_NVS_NOT_FOUND;
    if (*size<disk_size) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,disk,disk_size); *size=disk_size; return ESP_OK;
}
int nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size) {
    assert(size<sizeof(staged)); if(fail_set) return ESP_FAIL;
    memcpy(staged,data,size); staged_size=size; return ESP_OK;
}
int nvs_commit(nvs_handle_t h) {
    if(fail_commit) return ESP_FAIL;
    memcpy(disk,staged,staged_size); disk_size=staged_size; return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out) {
    if (!legacy_count) return ESP_ERR_NVS_NOT_FOUND; *out=legacy_count; return ESP_OK;
}
int nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size) {
    char text[64];
    snprintf(text,sizeof(text), "%s", key[0]=='s' ? "Legacy network" : " legacy pass ");
    size_t n=strlen(text)+1; if(*size<n) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out,text,n); *size=n; return ESP_OK;
}

/* SD backend double: writes are checked before updating the live cache. */
int launcher_store_read(const char *key,void *data,size_t *n){if(!disk_size)return ESP_ERR_NOT_FOUND;if(*n<disk_size)return ESP_ERR_INVALID_STATE;memcpy(data,disk,disk_size);*n=disk_size;return ESP_OK;}
int launcher_store_write(const char *key,const void *data,size_t n){if(fail_set || fail_commit)return ESP_FAIL;memcpy(disk,data,n);disk_size=n;return ESP_OK;}
/* Radio double: a disconnect request posts an asynchronous event, never synchronously. */
static wifi_status_t status;
static unsigned bits;
static bool disconnect_pending, connect_pending, wrong_order, drop_disconnect_ack;
static int connect_mode, pending_mode, auth_expire_remaining, connects, disconnects;
static unsigned waited_ms;
void vTaskDelay(unsigned ticks) { assert(status.state==WIFI_STATE_RECONNECTING); waited_ms+=ticks; }
static wifi_config_t configured;
unsigned xEventGroupClearBits(void *g,unsigned clear) { unsigned old=bits; bits &= ~clear; return old; }
unsigned xEventGroupWaitBits(void *g,unsigned wanted,int clear,int all,unsigned timeout) {
    if(disconnect_pending && !drop_disconnect_ack) {
        disconnect_pending=false;
        status.associated=false; status.ip[0]=0; status.state=WIFI_STATE_READY;
        status.disconnect_reason=8; bits |= WIFI_LINK_DISCONNECTED;
    } else if(connect_pending) {
        connect_pending=false;
        if(pending_mode==0) {
            status.state=WIFI_STATE_CONNECTED; status.associated=true;
            strcpy(status.ip,"192.0.2.10"); bits |= WIFI_LINK_CONNECTED;
        } else if(pending_mode==1 || pending_mode==3) {
            status.state=WIFI_STATE_READY; status.associated=false; status.ip[0]=0;
            status.disconnect_reason=pending_mode==3 ? 2 : 202; bits |= WIFI_LINK_DISCONNECTED;
        }
    }
    unsigned result=bits & wanted;
    if(clear) bits &= ~wanted;
    return result;
}
int esp_wifi_set_config(int iface,const wifi_config_t *config) { configured=*config; return ESP_OK; }
int esp_wifi_connect(void) {
    if(disconnect_pending) wrong_order=true;
    connects++; connect_pending=true;
    pending_mode = auth_expire_remaining > 0 ? 3 : connect_mode;
    if (auth_expire_remaining > 0) --auth_expire_remaining;
    return ESP_OK;
}
int esp_wifi_disconnect(void) { disconnects++; disconnect_pending=true; return ESP_OK; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { ap->rssi=-46; return ESP_OK; }
static void reset_radio(void) {
    memset(&status,0,sizeof(status)); status.state=WIFI_STATE_READY;
    bits=0; connects=disconnects=connect_mode=pending_mode=auth_expire_remaining=0; waited_ms=0;
    disconnect_pending=connect_pending=wrong_order=drop_disconnect_ack=false;
}
int main(void) {
    assert(wifi_saved_init()==ESP_OK);
    assert(wifi_saved_count()==0);
    char detail[160], pass[64];
    wifi_saved_network_t network={.ssid="  Home  SSID  ",.password="  p@ss $`()  ",.auth=7};
    reset_radio();
    assert(wifi_link_connect(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(connects==1 && disconnects==0); /* idle connection needs no synthetic disconnect */
    assert(status.network_saved && status.rssi==-46);
    assert(configured.sta.threshold.authmode==3);
    assert(wifi_saved_count()==1);
    assert(wifi_saved_password(network.ssid,pass,sizeof(pass)) && strcmp(pass,network.password)==0);
    wifi_saved_network_t loaded;
    assert(wifi_store_latest(&loaded) && loaded.auth==7 && strcmp(loaded.ssid,network.ssid)==0);
    status.state=WIFI_STATE_SCANNING;
    wifi_link_restore_state(&status);
    assert(status.state==WIFI_STATE_CONNECTED && strcmp(status.ip,"192.0.2.10")==0);
    /* Selecting the connected SSID does not interrupt traffic or save new, unverified input. */
    wifi_saved_network_t same=network; strcpy(same.password,"wrong input");
    assert(wifi_link_connect(&same,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(connects==1 && disconnects==0);
    assert(wifi_saved_password(network.ssid,pass,sizeof(pass)) && strcmp(pass,network.password)==0);
    wifi_saved_network_t other={.ssid="Second network",.password="12345678",.auth=3};
    /* The old disconnect must be consumed before starting a second connection. */
    assert(wifi_link_connect(&other,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(!wrong_order && connects==2 && disconnects==1 && !disconnect_pending);
    /* An AP rejection does not cause another disconnect that could poison the next attempt. */
    connect_mode=1;
    assert(wifi_link_connect(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_ERR_REFUSED);
    assert(strstr(detail,"202") && disconnects==2 && !disconnect_pending);
    assert(wifi_store_latest(&loaded) && strcmp(loaded.ssid,other.ssid)==0); /* failed creds not saved */
    connect_mode=0;
    assert(wifi_link_connect(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(!wrong_order && disconnects==2);
    /* A DHCP timeout is cleaned up once, with acknowledgment, then retry can succeed. */
    reset_radio(); connect_mode=2;
    assert(wifi_link_connect(&other,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_ERR_TIMEOUT);
    assert(disconnects==1 && !disconnect_pending);
    connect_mode=0;
    assert(wifi_link_connect(&other,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(!wrong_order && disconnects==1);
    /* Missing disconnect acknowledgment must not start a new attempt. */
    drop_disconnect_ack=true; int before=connects;
    assert(wifi_link_connect(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_ERR_TRANSPORT);
    assert(connects==before); reset_radio();
    /* Storage errors are reported without declaring a working network disconnected. */
    fail_set=1;
    assert(wifi_link_connect(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(!status.network_saved && status.state==WIFI_STATE_CONNECTED && detail[0]);
    assert(wifi_store_latest(&loaded) && strcmp(loaded.ssid,other.ssid)==0);
    fail_set=0;
    fail_commit=1; assert(wifi_store_put(&network)!=ESP_OK); fail_commit=0;
    assert(wifi_store_latest(&loaded) && strcmp(loaded.ssid,other.ssid)==0);
    /* AUTH_EXPIRE after reset is retried without changing the verified credential. */
    reset_radio(); auth_expire_remaining=2;
    assert(wifi_link_connect_retry(&network,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_OK);
    assert(connects==3 && disconnects==0 && waited_ms==30000 && status.network_saved);
    reset_radio(); auth_expire_remaining=9;
    assert(wifi_link_connect_retry(&other,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_ERR_REFUSED);
    assert(connects==3 && waited_ms==30000 && status.disconnect_reason==2);
    assert(wifi_store_latest(&loaded) && strcmp(loaded.ssid,network.ssid)==0);
    reset_radio(); connect_mode=1;
    assert(wifi_link_connect_retry(&other,&status,(void*)1,(void*)1,detail,sizeof(detail))==WIFI_RESULT_ERR_REFUSED);
    assert(connects==1 && waited_ms==0); /* other rejection causes no retry storm */
    /* Bounded retention, no duplicate SSIDs and durable forget. */
    for(int i=0;i<12;i++) { snprintf(network.ssid,sizeof(network.ssid),"AP%02d",i); assert(wifi_store_put(&network)==ESP_OK); }
    assert(wifi_saved_count()==WIFI_MAX_SAVED);
    assert(wifi_store_put(&network)==ESP_OK && wifi_saved_count()==WIFI_MAX_SAVED);
    wifi_forget(network.ssid); assert(wifi_saved_count()==WIFI_MAX_SAVED-1);
    wifi_forget_all(); assert(wifi_saved_count()==0);
    /* Malformed legacy count cannot overflow fixed credential arrays. */
    disk_size=0; legacy_count=255;wifi_saved_init();
    assert(wifi_saved_count()==0);
    assert(!wifi_store_latest(&loaded));
    status.state=WIFI_STATE_CONNECTED;status.associated=true;strcpy(status.ip,"1.2.3.4");wifi_link_disconnected(&status,8,true);assert(status.state==WIFI_STATE_DISABLED&&!status.associated&&!status.ip[0]);wifi_link_disconnected(&status,8,true);assert(status.state==WIFI_STATE_DISABLED);wifi_link_disconnected(&status,2,false);assert(status.state==WIFI_STATE_READY&&status.disconnect_reason==2);
    puts("Wi-Fi link/store passed: late disconnect, second attempt, scan state, timeout cleanup, saved credentials, NVS errors and bounds (ASan/UBSan).");
}
