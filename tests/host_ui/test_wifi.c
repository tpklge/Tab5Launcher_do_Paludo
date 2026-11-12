#include <assert.h>
#include <stdio.h>
void ui_style_refresh(void) {}
#include <string.h>
#include "ui.h"
#include "wifi_mgr.h"
#include "input.h"
static wifi_scan_cb_t pending_scan;
static wifi_connect_cb_t pending_connect;
static void *scan_user, *connect_user;
static int connects, scans;
static wifi_status_t fake_status = {.state = WIFI_STATE_READY};
static bool have_saved_password;
static char received_ssid[33], received_password[64];
void wifi_get_status(wifi_status_t *s) { *s = fake_status; }
esp_err_t wifi_scan(wifi_scan_cb_t cb, void *user) { pending_scan=cb; scan_user=user; scans++; return ESP_OK; }
esp_err_t wifi_connect(const char *ssid,const char *pass,uint8_t auth,wifi_connect_cb_t cb,void *user) {
    snprintf(received_ssid,sizeof(received_ssid),"%s",ssid);
    snprintf(received_password,sizeof(received_password),"%s",pass);
    assert(auth==7); connects++; pending_connect=cb; connect_user=user; return ESP_OK;
}
esp_err_t wifi_disconnect(void) { return ESP_OK; }
bool wifi_saved_password(const char *ssid, char *pass, size_t len) {
    if (!have_saved_password) return false;
    snprintf(pass,len," saved $pass "); return true;
}
bool input_kbd_present(void) { return true; }
bool wifi_auth_supported(uint8_t auth) { return auth==0 || auth==3 || auth==7; }
bool wifi_auth_needs_password(uint8_t auth) { return auth!=0; }
bool wifi_password_too_short(uint8_t auth,const char *pass) { return auth && strlen(pass)<8; }
const char *wifi_auth_name(uint8_t auth) { return auth==7 ? "WPA2/WPA3" : "WPA2"; }
#include "../../components/ui/ui_wifi.c"
static uint32_t pixels[1280*720];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *data) { lv_display_flush_ready(display); }
static void tick(void) { lv_tick_inc(120); lv_timer_handler(); }
static void results(void) {
    wifi_ap_t aps[16]={0};
    for(int i=0;i<16;i++) {
        snprintf(aps[i].ssid,33,"Rede %02d",i); aps[i].rssi=-30-i; aps[i].auth=3;
    }
    strcpy(aps[0].ssid,"Minha  rede com espacos"); aps[0].auth=7;
    assert(pending_scan); pending_scan(ESP_OK,aps,16,scan_user); tick();
}
static void open_first(void) { lv_obj_send_event(lv_obj_get_child(s_list,0),LV_EVENT_CLICKED,NULL); }
static void shot(const char *file) {
    lv_refr_now(NULL);
    FILE *f=fopen(file,"wb"); assert(f); fprintf(f,"P6\n1280 720\n255\n");
    for(int i=0;i<1280*720;i++) { unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]}; fwrite(rgb,1,3,f); }
    fclose(f);
}
extern bool input_hid_to_key(uint8_t, uint8_t, key_event_t *);
int main(void) {
    key_event_t mapped;
    assert(input_hid_to_key(0,0x2C,&mapped) && mapped.ch==' ');
    assert(input_hid_to_key(2,0x1F,&mapped) && mapped.ch=='@');
    assert(input_hid_to_key(0,0x31,&mapped) && mapped.ch=='\\');
    assert(input_hid_to_key(2,0x34,&mapped) && mapped.ch=='"');
    assert(!input_hid_to_key(0,0,&mapped));
    lv_init();
    lv_display_t *display=lv_display_create(1280,720);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display,pixels,NULL,sizeof(pixels),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    ui_nav_init(lv_screen_active());
    ui_wifi_create(lv_screen_active()); tick(); results(); lv_obj_update_layout(s_page);
    assert(lv_obj_get_height(s_list)>450);
    assert(lv_obj_get_child_count(s_list)==16);
    shot("wifi-list.ppm");
    open_first(); assert(s_modal && s_password);
    assert(lv_obj_has_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN));
    show_keyboard_cb(NULL); lv_obj_update_layout(s_modal);
    assert(!lv_obj_has_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN));
    lv_obj_t *panel=lv_obj_get_child(s_modal,0);
    lv_area_t pa,ka; lv_obj_get_coords(panel,&pa); lv_obj_get_coords(s_keyboard,&ka);
    assert(pa.y2 < ka.y1);
    lv_obj_send_event(s_keyboard,LV_EVENT_READY,NULL);
    assert(lv_obj_has_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN));
    key_event_t enter={.kind=KEY_KIND_ENTER};
    ui_wifi_handle_key(&enter); assert(connects==0 && s_modal);
    const char *password=" Ab$`() 12 ";
    for(const char *p=password;*p;p++) { key_event_t key={.kind=KEY_KIND_CHAR,.ch=*p}; assert(ui_wifi_handle_key(&key)); }
    assert(strcmp(lv_textarea_get_text(s_password),password)==0);
    shot("wifi-dialog.ppm");
    ui_wifi_handle_key(&enter); assert(connects==1 && !s_modal);
    assert(strcmp(received_password,password)==0);
    assert(strcmp(received_ssid,"Minha  rede com espacos")==0);
    /* A late response from a deleted page must not update the next page. */
    wifi_connect_cb_t old_cb=pending_connect; void *old_user=connect_user;
    lv_obj_clean(lv_screen_active());
    ui_wifi_create(lv_screen_active()); tick();
    old_cb(WIFI_RESULT_OK,NULL,old_user); tick();
    assert(strstr(lv_label_get_text(s_status),"Conectado")==NULL);
    for(int i=0;i<100;i++) {
        results(); open_first();
        key_event_t esc={.kind=KEY_KIND_ESC}; ui_wifi_handle_key(&esc); assert(!s_modal);
        open_first(); lv_obj_clean(lv_screen_active()); assert(!s_modal && !s_poll);
        ui_wifi_create(lv_screen_active()); tick();
    }
    /* Returning to Wi-Fi preserves the connection and cached rows without a new scan. */
    int before_scans = scans;
    fake_status.state = WIFI_STATE_CONNECTED;
    fake_status.associated = true;
    strcpy(fake_status.ssid,"Minha  rede com espacos");
    strcpy(fake_status.ip,"192.0.2.10");
    lv_obj_clean(lv_screen_active()); ui_wifi_create(lv_screen_active()); tick();
    assert(scans == before_scans && strstr(lv_label_get_text(s_status),"192.0.2.10"));
    open_first(); assert(!s_modal && connects == 1);
    fake_status.state = WIFI_STATE_READY; fake_status.associated = false; fake_status.ip[0]=0;
    have_saved_password = true;
    open_first(); assert(s_password && strcmp(lv_textarea_get_text(s_password)," saved $pass ")==0);
    key_event_t esc_saved={.kind=KEY_KIND_ESC}; ui_wifi_handle_key(&esc_saved);
    lv_obj_clean(lv_screen_active()); free(s_replies); s_replies=NULL;
    lv_display_delete(display); lv_deinit();
    puts("Wi-Fi UI: geometry, mixed auth, physical keys, confirmation, keyboard dismissal, late replies and 100 navigation cycles passed.");
}
