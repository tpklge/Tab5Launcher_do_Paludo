/* Wi-Fi UI. Only the LVGL task owns objects; worker replies are copied to a queue. */
#include "ui.h"
#include "ui_nav.h"
#include "wifi_mgr.h"
#include "input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

#define BG 0x14171C
#define PANEL 0x20242B
#define ACCENT 0xEB2638
#define MUTED 0x9CA6B5

typedef struct {
    unsigned generation;
    bool connection;
    esp_err_t error;
    wifi_result_t result;
    int count;
    wifi_ap_t aps[WIFI_MAX_SCAN];
    char message[160];
} reply_t;
static QueueHandle_t s_replies;
static lv_obj_t *s_page, *s_list, *s_status, *s_modal, *s_password, *s_keyboard, *s_hint;
static lv_timer_t *s_poll;
static unsigned s_generation;
static wifi_ap_t s_aps[WIFI_MAX_SCAN];
static int s_count;
static wifi_ap_t s_selected;
static bool s_initial_scan;
static wifi_state_t s_last_state;

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, 9, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x303640), 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x242830), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x454C58), 0);
    lv_obj_set_style_shadow_width(obj, 8, 0);
    lv_obj_set_style_shadow_color(obj, lv_color_hex(0x080A0D), 0);
    lv_obj_t *text_obj = lv_label_create(obj);
    lv_label_set_text(text_obj, text);
    lv_obj_center(text_obj);
    if (cb) lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
    return obj;
}
static void close_dialog(void)
{
    if (!s_modal) return;
    lv_obj_t *modal = s_modal;
    s_modal = s_password = s_keyboard = s_hint = NULL;
    lv_obj_delete(modal);
}
static void close_cb(lv_event_t *e) { close_dialog(); }
static void scan_reply(esp_err_t err, const wifi_ap_t *aps, int count, void *user)
{
    reply_t reply = {.generation = (unsigned)(uintptr_t)user, .error = err};
    reply.count = count < WIFI_MAX_SCAN ? count : WIFI_MAX_SCAN;
    if (reply.count < 0 || !aps) reply.count = 0;
    if (reply.count) memcpy(reply.aps, aps, reply.count * sizeof(*aps));
    if (s_replies) xQueueSend(s_replies, &reply, 0);
}
static void connect_reply(wifi_result_t result, const char *msg, void *user)
{
    reply_t reply = {.generation = (unsigned)(uintptr_t)user, .connection = true, .result = result};
    if (msg) snprintf(reply.message, sizeof(reply.message), "%s", msg);
    if (s_replies) xQueueSend(s_replies, &reply, 0);
}
static void connect_cb(lv_event_t *e)
{
    if (!s_password) return;
    const char *password = lv_textarea_get_text(s_password);
    if (wifi_password_too_short(s_selected.auth, password)) {
        lv_label_set_text(s_hint, "A senha precisa ter pelo menos 8 caracteres.");
        return;
    }
    esp_err_t err = wifi_connect(s_selected.ssid, password, s_selected.auth,
                                 connect_reply, (void *)(uintptr_t)s_generation);
    if (err != ESP_OK) {
        lv_label_set_text(s_hint, "Wi-Fi ocupado. Aguarde e tente novamente.");
        return;
    }
    lv_label_set_text_fmt(s_status, "Conectando a %s...", s_selected.ssid);
    close_dialog();
}
static void keyboard_cb(lv_event_t *e)
{
    /* The screen keyboard's check mark hides it, keeping the Connect button visible. */
    if (s_keyboard) lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void show_keyboard_cb(lv_event_t *e)
{
    if (s_keyboard) lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void show_password_cb(lv_event_t *e)
{
    if (s_password) lv_textarea_set_password_mode(s_password,
        !lv_textarea_get_password_mode(s_password));
}
static void ap_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (index < 0 || index >= s_count) return;
    if (!wifi_auth_supported(s_aps[index].auth)) {
        lv_label_set_text_fmt(s_status, "Seguranca %s ainda nao suportada. Escolha uma rede pessoal WPA/WPA2/WPA3.",
                              wifi_auth_name(s_aps[index].auth));
        return;
    }
    wifi_status_t current; wifi_get_status(&current);
    if (current.state == WIFI_STATE_CONNECTED && strcmp(current.ssid, s_aps[index].ssid) == 0) {
        lv_label_set_text_fmt(s_status, "Ja conectado: %s    IP %s", current.ssid, current.ip);
        return;
    }
    close_dialog();
    s_selected = s_aps[index];
    s_modal = lv_obj_create(lv_layer_top());
    ui_nav_modal(s_modal);
    lv_obj_add_event_cb(s_modal,close_cb,LV_EVENT_CANCEL,NULL);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x06080C), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_90, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_set_style_pad_all(s_modal, 0, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_modal);
    lv_obj_set_size(panel, 760, 330);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_color(panel, lv_color_hex(PANEL), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_pad_all(panel, 22, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(panel, s_selected.ssid, 0, 0, 0xFFFFFF);
    lv_obj_set_width(title, 690);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    label(panel, wifi_auth_needs_password(s_selected.auth) ? "Senha da rede" : "Rede aberta - senha dispensada", 0, 44, MUTED);
    s_password = lv_textarea_create(panel);
    lv_obj_set_pos(s_password, 0, 70);
    lv_obj_set_size(s_password, 698, 54);
    lv_textarea_set_one_line(s_password, true);
    lv_textarea_set_password_mode(s_password, true);
    lv_textarea_set_max_length(s_password, 63);
    char saved_password[64] = {0};
    if (wifi_saved_password(s_selected.ssid, saved_password, sizeof(saved_password)))
        lv_textarea_set_text(s_password, saved_password);
    memset(saved_password, 0, sizeof(saved_password));
    lv_textarea_set_placeholder_text(s_password, "Digite no teclado fisico ou toque em Teclado");
    lv_obj_set_style_border_color(s_password, lv_color_hex(ACCENT), 0);
    lv_obj_add_state(s_password, LV_STATE_FOCUSED);
    s_hint = label(panel, "Enter: conectar    Esc: cancelar    Senha preserva espacos e simbolos", 0, 136, MUTED);
    lv_obj_set_width(s_hint, 698);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_t *show = button(panel, "Mostrar senha", 168, 42, show_password_cb);
    lv_obj_set_pos(show, 0, 174);
    lv_obj_t *kb = button(panel, LV_SYMBOL_KEYBOARD "  Teclado", 150, 42, show_keyboard_cb);
    lv_obj_set_pos(kb, 180, 174);
    lv_obj_t *cancel = button(panel, "Cancelar", 150, 48, close_cb);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *connect = button(panel, "Conectar  " LV_SYMBOL_RIGHT, 210, 48, connect_cb);
    lv_obj_add_flag(connect,LV_OBJ_FLAG_USER_2);
    lv_obj_align(connect, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(connect, lv_color_hex(ACCENT), 0);
    lv_obj_set_style_bg_grad_color(connect, lv_color_hex(0xA51226), 0);
    s_keyboard = lv_keyboard_create(s_modal);
    lv_obj_set_size(s_keyboard, LV_PCT(100), 300);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_keyboard, s_password);
    lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
    if (input_kbd_present()) lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    ui_nav_refresh(); ui_nav_focus(s_password);
}

bool ui_wifi_handle_key(const key_event_t *key)
{
    if (!s_modal || !s_password) return false;
    if (key->kind == KEY_KIND_ESC) { close_dialog(); return true; }
    lv_obj_t *focused=ui_nav_focused();
    if (key->kind==KEY_KIND_TAB || (focused && focused!=s_password)) return false;
    if (key->kind == KEY_KIND_ENTER) connect_cb(NULL);
    else if (key->kind == KEY_KIND_ESC) close_dialog();
    else if (key->kind == KEY_KIND_BACKSPACE) lv_textarea_delete_char(s_password);
    else if (key->kind == KEY_KIND_DELETE) lv_textarea_delete_char_forward(s_password);
    else if (key->kind == KEY_KIND_LEFT) lv_textarea_cursor_left(s_password);
    else if (key->kind == KEY_KIND_RIGHT) lv_textarea_cursor_right(s_password);
    else if (key->kind == KEY_KIND_HOME) lv_textarea_set_cursor_pos(s_password, 0);
    else if (key->kind == KEY_KIND_END) lv_textarea_set_cursor_pos(s_password, LV_TEXTAREA_CURSOR_LAST);
    else if (key->kind == KEY_KIND_CHAR && !key->ctrl && !key->alt) {
        uint32_t ch = key_char(key);
        if (ch >= 32 && ch < 127) { char text[2] = {(char)ch, 0}; lv_textarea_add_text(s_password, text); }
    }
    return true;
}

static void start_scan(void)
{
    if (wifi_scan(scan_reply, (void *)(uintptr_t)s_generation) == ESP_OK)
        lv_label_set_text(s_status, "Procurando redes de 2,4 GHz...");
    else lv_label_set_text(s_status, "Wi-Fi iniciando ou ocupado. Aguarde e tente novamente.");
}
static void scan_cb(lv_event_t *e) { start_scan(); }
static void disconnect_cb(lv_event_t *e)
{
    if (wifi_disconnect() == ESP_OK) lv_label_set_text(s_status, "Desconectando...");
}
static void populate(void)
{
    lv_obj_clean(s_list);
    for (int i = 0; i < s_count; ++i) {
        lv_obj_t *row = button(s_list, "", 100, 62, NULL);
        /* Replace the helper's empty label with a two-line network plate. */
        lv_obj_clean(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_style_pad_all(row, 10, 0);
        label(row, LV_SYMBOL_WIFI, 4, 9, ACCENT);
        lv_obj_t *name = label(row, s_aps[i].ssid[0] ? s_aps[i].ssid : "(rede oculta)", 44, 0, 0xFFFFFF);
        lv_obj_set_width(name, 600);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        char info[64];
        snprintf(info, sizeof(info), "%s   /   %d dBm", wifi_auth_name(s_aps[i].auth), s_aps[i].rssi);
        label(row, info, 44, 24, MUTED);
        lv_obj_t *arrow = label(row, LV_SYMBOL_RIGHT, 0, 0, MUTED);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_add_event_cb(row, ap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    ui_nav_refresh();
}
static void poll_cb(lv_timer_t *timer)
{
    reply_t reply;
    while (s_replies && xQueueReceive(s_replies, &reply, 0) == pdTRUE) {
        if (reply.generation != s_generation) continue;
        if (reply.connection) {
            if (reply.result == WIFI_RESULT_OK) {
                wifi_status_t status; wifi_get_status(&status);
                if (reply.message[0]) lv_label_set_text(s_status, reply.message);
                else lv_label_set_text_fmt(s_status, "Conectado: %s    IP %s%s", status.ssid, status.ip,
                                           status.network_saved ? "  /  rede salva" : "");
            } else lv_label_set_text_fmt(s_status, "Nao conectado: %s", reply.message);
        } else {
            if (reply.error != ESP_OK) { lv_label_set_text(s_status, "Falha na busca. Tente novamente."); continue; }
            s_count = reply.count;
            memcpy(s_aps, reply.aps, sizeof(s_aps));
            populate();
            wifi_status_t current; wifi_get_status(&current);
            if (current.associated && current.ip[0])
                lv_label_set_text_fmt(s_status, "Conectado: %s    IP %s  /  %d redes", current.ssid, current.ip, s_count);
            else lv_label_set_text_fmt(s_status, "%d redes encontradas  /  selecione para conectar", s_count);
        }
    }
    wifi_status_t status; wifi_get_status(&status);
    if (status.state != s_last_state) {
        s_last_state = status.state;
        if (status.state == WIFI_STATE_FAILED)
            lv_label_set_text_fmt(s_status, "Wi-Fi indisponivel: %s", status.error);
        else if (status.state == WIFI_STATE_CONNECTED)
            lv_label_set_text_fmt(s_status, "Conectado: %s    IP %s", status.ssid, status.ip);
        else if (status.state == WIFI_STATE_RECONNECTING)
            lv_label_set_text(s_status, status.error[0] ? status.error : "Tentando conectar novamente...");
        else if (status.state == WIFI_STATE_AUTHENTICATING || status.state == WIFI_STATE_GETTING_IP)
            lv_label_set_text_fmt(s_status, "Conectando a %s...", status.ssid);
        else if (status.state == WIFI_STATE_READY) {
            if (status.disconnect_reason)
                lv_label_set_text_fmt(s_status, "Desconectado (motivo %u). Selecione a rede ou aguarde a reconexao.", status.disconnect_reason);
            else lv_label_set_text(s_status, "Selecione uma rede para conectar. Redes salvas dispensam redigitar a senha.");
        }
    }
    if (s_initial_scan && status.state == WIFI_STATE_CONNECTED) s_initial_scan = false;
    if (s_initial_scan && status.state == WIFI_STATE_READY) {
        s_initial_scan = false;
        start_scan();
    }
}
static void delete_cb(lv_event_t *e)
{
    if (s_poll) { lv_timer_delete(s_poll); s_poll = NULL; }
    close_dialog();
    s_page = s_list = s_status = NULL;
    ++s_generation;
}

lv_obj_t *ui_wifi_create(lv_obj_t *parent)
{
    ++s_generation;
    s_initial_scan = s_count == 0;
    s_last_state = WIFI_STATE_DISABLED;
    if (!s_replies) s_replies = xQueueCreate(4, sizeof(reply_t));
    s_page = lv_obj_create(parent);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_page, lv_color_hex(BG), 0);
    lv_obj_set_style_pad_all(s_page, 24, 0);
    lv_obj_set_style_border_width(s_page, 0, 0);
    lv_obj_clear_flag(s_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_page, delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = label(s_page, "REDES WI-FI", 0, 0, 0xFFFFFF);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    label(s_page, "Conectividade / ESP32-C6", 0, 34, MUTED);
    lv_obj_t *scan = button(s_page, LV_SYMBOL_REFRESH "  Buscar redes", 180, 48, scan_cb);
    lv_obj_align(scan, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(scan, lv_color_hex(ACCENT), 0);
    lv_obj_t *disconnect = button(s_page, "Desconectar", 160, 48, disconnect_cb);
    lv_obj_align(disconnect, LV_ALIGN_TOP_RIGHT, -194, 0);
    s_status = label(s_page, "Iniciando Wi-Fi...", 0, 76, 0xC9D2E1);
    lv_obj_set_width(s_status, LV_PCT(100));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    lv_obj_update_layout(s_page);
    s_list = lv_obj_create(s_page);
    lv_obj_set_pos(s_list, 0, 112);
    lv_obj_set_size(s_list, lv_obj_get_content_width(s_page), lv_obj_get_content_height(s_page) - 142);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 8, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t *footer = label(s_page, "TECLADO FISICO  /  Enter conecta  /  Esc cancela  /  toque na rede para informar a senha", 0, 0, MUTED);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (s_count > 0) populate();
    if (!s_replies) lv_label_set_text(s_status, "Memoria insuficiente para a busca.");
    else s_poll = lv_timer_create(poll_cb, 100, NULL);
    return s_page;
}
