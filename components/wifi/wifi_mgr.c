/* wifi_mgr.c — Wi-Fi via ESP32-C6 / ESP-Hosted 2.12.6.
 *
 * A single worker task owns the C6 and processes requests from a command queue.
 * State transitions call the registered listener with the LVGL lock held.
 *
 * SDIO bus note: ESP-Hosted uses SDIO slot 1 (CLK=12, CMD=13, D0-D3=11,10,9,8, RST=15
 * from bmorcelli Tab5 platformio.ini).  The SD card uses SDMMC slot 0 (separate pins).
 * They do NOT share a bus; both can run concurrently on the Tab5.
 *
 * Credentials: versioned records in /launcher/private on the SD card.
 * SSID and password are stored verbatim — no trim, no case folding.
 */
#include "wifi_mgr.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#include "wifi_link.h"
#include "board.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_timer.h"

/* esp_wifi_remote injects WIFI_INIT_CONFIG_DEFAULT() which uses CONFIG_WIFI_RMT_*
 * symbols from sdkconfig. On ESP32-P4 these come from the slave (C6) config via
 * esp_wifi_default_config.h, but the Kconfig chain may not propagate them correctly
 * when IDF < 5.5. Define the C6 defaults here as a fallback. */
#ifndef CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_STATIC_RX_BUFFER_NUM   10
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM
#define CONFIG_WIFI_RMT_DYNAMIC_RX_BUFFER_NUM  32
#endif
#ifndef CONFIG_WIFI_RMT_TX_BUFFER_TYPE
#define CONFIG_WIFI_RMT_TX_BUFFER_TYPE          0
#endif
#ifndef CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF
#define CONFIG_WIFI_RMT_DYNAMIC_RX_MGMT_BUF    0
#endif
#ifndef CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM
#define CONFIG_WIFI_RMT_ESPNOW_MAX_ENCRYPT_NUM  7
#endif

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_hosted.h"
#include "esp_lvgl_port.h"

static const char *TAG = "wifi_mgr";

#define WORKER_STACK    12288
#define CMD_QUEUE_SIZE  4

/* ---- State --------------------------------------------------------------- */

static wifi_status_t  s_status;
static wifi_listener_t s_listener = NULL;
static SemaphoreHandle_t s_mutex = NULL;  /* protects s_status */
static TaskHandle_t  s_worker = NULL;
static QueueHandle_t s_cmd_q  = NULL;
static bool          s_initialized = false;
static bool          s_autoconnect = false;
static wifi_scan_cb_t s_pending_scan_cb   = NULL;
static void          *s_pending_scan_user = NULL;
static SemaphoreHandle_t s_transport_up_sem = NULL;
static EventGroupHandle_t s_connection_events;
#define CONNECTED_BIT WIFI_LINK_CONNECTED
#define DISCONNECTED_BIT WIFI_LINK_DISCONNECTED
static wifi_saved_network_t s_active_network;
static bool s_reconnect_allowed;
static unsigned s_retry_count;
static int64_t s_retry_at;
static atomic_bool s_paused,s_pause_waiting;
static atomic_uint s_pause_issued,s_pause_completed;
static atomic_int s_pause_result;

static void connection_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if(atomic_load(&s_paused) && !(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED))return;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_status.associated = true;
        s_status.state = WIFI_STATE_CONNECTED;
        xSemaphoreGive(s_mutex);
        xEventGroupSetBits(s_connection_events, CONNECTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.associated = true;
        s_status.state = WIFI_STATE_GETTING_IP;
        xSemaphoreGive(s_mutex);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        ESP_LOGW(TAG, "station disconnected, reason=%u", event ? event->reason : 0);
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        wifi_link_disconnected(&s_status,event?event->reason:0,atomic_load(&s_paused));
        xSemaphoreGive(s_mutex);
        xEventGroupSetBits(s_connection_events, DISCONNECTED_BIT);
    }
}

/* Command types sent to the worker */
typedef enum {
    CMD_START,
    CMD_SCAN,
    CMD_SCAN_DONE,   /* posted by WIFI_EVENT_SCAN_DONE event handler */
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_REFRESH_SIGNAL,
    CMD_RETRY,
    CMD_RECONNECT,
    CMD_RESTART_DRIVER,
    CMD_RESET_C6,
    CMD_PAUSE,
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    unsigned pause_id;
    char ssid[33];
    char pass[64];
    uint8_t auth;
    wifi_scan_cb_t    scan_cb;
    wifi_connect_cb_t conn_cb;
    void *user;
} wifi_cmd_t;

/* ---- Event handlers ------------------------------------------------------ */

static void scan_done_event_handler(void *arg, esp_event_base_t base,
                                    int32_t id, void *data)
{
    wifi_cmd_t c = { .type = CMD_SCAN_DONE };
    xQueueSend(s_cmd_q, &c, 0);
}

static void hosted_event_handler(void *arg, esp_event_base_t base,
                                 int32_t id, void *data)
{
    if (id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        ESP_LOGI(TAG, "ESP-Hosted transport UP");
        if (s_transport_up_sem) xSemaphoreGive(s_transport_up_sem);
    } else if (id == ESP_HOSTED_EVENT_TRANSPORT_DOWN ||
               id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
        ESP_LOGW(TAG, "ESP-Hosted transport DOWN/FAILURE");
    }
}

/* ---- Listener helper ----------------------------------------------------- */

static void notify_listener(void)
{
    if (!s_listener) return;
    if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
        s_listener();
        lvgl_port_unlock();
    }
}

static void set_state(wifi_state_t state)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = state;
    xSemaphoreGive(s_mutex);
    notify_listener();
}

/* ---- Worker -------------------------------------------------------------- */

static void wifi_worker(void *arg)
{
    wifi_cmd_t cmd;

    /* Wait for CMD_START */
    xQueueReceive(s_cmd_q, &cmd, portMAX_DELAY);
    if (cmd.type != CMD_START) goto idle;

    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) goto failed;

    /* Power on C6 and give it a moment to start its bootloader */
    set_state(WIFI_STATE_INIT_C6);
    esp_err_t pwr_err = board_c6_power(true);
    if (pwr_err != ESP_OK) {
        ESP_LOGE(TAG, "C6 power-on failed: %s", esp_err_to_name(pwr_err));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.state = WIFI_STATE_FAILED;
        snprintf(s_status.error, sizeof(s_status.error),
                 "C6 power-on failed: %s", esp_err_to_name(pwr_err));
        xSemaphoreGive(s_mutex);
        notify_listener();
        goto idle;
    }
    vTaskDelay(pdMS_TO_TICKS(500));  /* 500ms for C6 bootloader; transport_up event signals readiness */

    /* Connect to C6. esp_hosted_init() ran at boot via __attribute__((constructor)).
     * We just need to connect to the now-powered slave and wait for TRANSPORT_UP. */
    set_state(WIFI_STATE_TRANSPORT);

    /* Register hosted event handler before calling connect so we don't miss the event */
    esp_event_handler_register(ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID,
                               hosted_event_handler, NULL);

    esp_err_t err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_hosted_connect_to_slave: %s", esp_err_to_name(err));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.state = WIFI_STATE_FAILED;
        snprintf(s_status.error, sizeof(s_status.error),
                 "ESP-Hosted connect failed: %s", esp_err_to_name(err));
        xSemaphoreGive(s_mutex);
        notify_listener();
        goto idle;
    }

    /* Wait for transport to be up — TRANSPORT_UP event releases s_transport_up_sem */
    ESP_LOGI(TAG, "Waiting for ESP-Hosted transport UP...");
    if (xSemaphoreTake(s_transport_up_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for ESP-Hosted transport");
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_status.state = WIFI_STATE_FAILED;
        snprintf(s_status.error, sizeof(s_status.error), "ESP-Hosted transport timeout");
        xSemaphoreGive(s_mutex);
        notify_listener();
        goto idle;
    }
    ESP_LOGI(TAG, "ESP-Hosted transport UP, initializing WiFi");

    /* Init TCP/IP stack and Wi-Fi (esp_event_loop may already exist — ignore error) */
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_cfg.nvs_enable = false;
    err = esp_wifi_init(&wifi_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        goto failed;
    }
    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                     connection_event_handler, NULL);
    if (err != ESP_OK) goto failed;
    err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
                                     connection_event_handler, NULL);
    if (err != ESP_OK) goto failed;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     connection_event_handler, NULL);
    if (err != ESP_OK) goto failed;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) goto failed;
    err = esp_wifi_start();
    if (err != ESP_OK) goto failed;

    /* Post CMD_SCAN_DONE to worker queue when scan completes */
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                               scan_done_event_handler, NULL);

    esp_hosted_coprocessor_fwver_t version={0};
    if(esp_hosted_get_coprocessor_fwversion(&version)==ESP_OK) {
        xSemaphoreTake(s_mutex,portMAX_DELAY);
        snprintf(s_status.c6_fw,sizeof(s_status.c6_fw),"%u.%u.%u",(unsigned)(version.major1&255),(unsigned)(version.minor1&255),(unsigned)(version.patch1&255));
        s_status.c6_mismatch=version.major1!=ESP_HOSTED_VERSION_MAJOR_1 || version.minor1!=ESP_HOSTED_VERSION_MINOR_1;
        xSemaphoreGive(s_mutex);
    }
    set_state(WIFI_STATE_READY);
    ESP_LOGI(TAG, "Wi-Fi ready");

    if (s_autoconnect && wifi_store_latest(&s_active_network)) {
        wifi_cmd_t auto_cmd = {.type = CMD_CONNECT, .auth = s_active_network.auth};
        memcpy(auto_cmd.ssid, s_active_network.ssid, sizeof(auto_cmd.ssid));
        memcpy(auto_cmd.pass, s_active_network.password, sizeof(auto_cmd.pass));
        xQueueSend(s_cmd_q, &auto_cmd, 0);
        ESP_LOGI(TAG, "restoring saved network");
    }

idle:
    while (1) {
        if (xQueueReceive(s_cmd_q, &cmd, pdMS_TO_TICKS(250)) != pdTRUE) {
            wifi_status_t current;
            wifi_get_status(&current);
            if (current.state == WIFI_STATE_CONNECTED) { s_retry_count = 0; s_retry_at = 0; }
            if (!s_reconnect_allowed || current.state != WIFI_STATE_READY || s_retry_count >= 3) continue;
            int64_t now = esp_timer_get_time();
            if (!s_retry_at) s_retry_at = now + (int64_t)(1u << s_retry_count) * 1000000;
            if (now < s_retry_at) continue;
            s_retry_at = 0;
            ++s_retry_count;
            ESP_LOGI(TAG, "reconnecting after link loss, attempt %u/3", s_retry_count);
            char detail[160];
            wifi_link_connect(&s_active_network, &s_status, s_mutex, s_connection_events, detail, sizeof(detail));
            continue;
        }

        switch (cmd.type) {
        case CMD_START: {
            wifi_status_t current;wifi_get_status(&current);
            if(current.state==WIFI_STATE_DISABLED) {
                atomic_store(&s_paused,false);
                esp_err_t resumed=esp_wifi_start();
                set_state(resumed==ESP_OK?WIFI_STATE_READY:WIFI_STATE_FAILED);
                if(resumed==ESP_OK && s_active_network.ssid[0]){wifi_cmd_t reconnect={.type=CMD_RECONNECT};xQueueSend(s_cmd_q,&reconnect,0);}

            }
            break;
        }

        case CMD_RETRY:
        case CMD_RECONNECT:
            if (!wifi_store_latest(&s_active_network)) break;
            memcpy(cmd.ssid, s_active_network.ssid, sizeof(cmd.ssid));
            memcpy(cmd.pass, s_active_network.password, sizeof(cmd.pass));
            cmd.auth = s_active_network.auth;
            /* fall through */
        case CMD_CONNECT: {
            s_reconnect_allowed = false;
            wifi_status_t current;
            wifi_get_status(&current);
            if (current.state == WIFI_STATE_SCANNING || current.state == WIFI_STATE_FAILED ||
                current.state == WIFI_STATE_DISABLED) {
                if (cmd.conn_cb) cmd.conn_cb(WIFI_RESULT_ERR_TRANSPORT, "Wi-Fi ocupado ou indisponivel", cmd.user);
                break;
            }
            wifi_saved_network_t network = {.auth = cmd.auth};
            memcpy(network.ssid, cmd.ssid, sizeof(network.ssid));
            memcpy(network.password, cmd.pass, sizeof(network.password));
            bool already_connected = current.associated && current.ip[0] &&
                                     strcmp(current.ssid, network.ssid) == 0;
            char detail[160];
            wifi_result_t result = wifi_link_connect_retry(&network, &s_status, s_mutex,
                                                      s_connection_events, detail, sizeof(detail));
            if (result == WIFI_RESULT_OK) {
                /* Reuse verified credentials rather than user input when already connected. */
                if (!already_connected) s_active_network = network;
                s_reconnect_allowed = s_active_network.ssid[0] != 0;
                s_retry_count = 0;
                s_retry_at = 0;
            }
            if (cmd.conn_cb) cmd.conn_cb(result, detail[0] ? detail : NULL, cmd.user);
            memset(cmd.pass, 0, sizeof(cmd.pass));
            notify_listener();
            break;
        }
        case CMD_DISCONNECT:
            s_reconnect_allowed = false;
            ESP_LOGI(TAG, "manual disconnect requested");
            esp_wifi_disconnect();
            break;
        case CMD_SCAN: {
            if (s_pending_scan_cb) {
                if (cmd.scan_cb) cmd.scan_cb(ESP_ERR_INVALID_STATE, NULL, 0, cmd.user);
                break;
            }
            set_state(WIFI_STATE_SCANNING);
            wifi_scan_config_t scfg = { .show_hidden = false };
            /* Non-blocking scan: results collected in CMD_SCAN_DONE sent by event handler */
            s_pending_scan_cb   = cmd.scan_cb;
            s_pending_scan_user = cmd.user;
            esp_err_t serr = esp_wifi_scan_start(&scfg, false);
            if (serr != ESP_OK) {
                s_pending_scan_cb = NULL;
                if (cmd.scan_cb) cmd.scan_cb(serr, NULL, 0, cmd.user);
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if(atomic_load(&s_paused))s_status.state=WIFI_STATE_DISABLED;else wifi_link_restore_state(&s_status);
                xSemaphoreGive(s_mutex);
            }
            /* Results arrive via CMD_SCAN_DONE from the WIFI_EVENT_SCAN_DONE handler */
            break;
        }
        case CMD_SCAN_DONE: {
            /* Use static buffers — wifi_ap_record_t is ~136 B × 16 = 2 KB,
             * too large for the worker stack. */
            static EXT_RAM_BSS_ATTR wifi_ap_record_t records[WIFI_MAX_SCAN];
            static EXT_RAM_BSS_ATTR wifi_ap_t aps[WIFI_MAX_SCAN];
            uint16_t count = WIFI_MAX_SCAN;
            esp_err_t scan_err = esp_wifi_scan_get_ap_records(&count, records);
            if (scan_err != ESP_OK) count = 0;
            ESP_LOGI(TAG, "scan complete: %s, %u networks", esp_err_to_name(scan_err), (unsigned)count);
            for (int i = 0; i < count; i++) {
                strncpy(aps[i].ssid, (char*)records[i].ssid, 32);
                aps[i].ssid[32] = '\0';
                aps[i].rssi    = records[i].rssi;
                aps[i].channel = records[i].primary;
                aps[i].auth    = records[i].authmode;
            }
            wifi_scan_cb_t cb   = s_pending_scan_cb;
            void          *user = s_pending_scan_user;
            s_pending_scan_cb   = NULL;
            s_pending_scan_user = NULL;
            if (cb) cb(scan_err, aps, count, user);
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if(atomic_load(&s_paused))s_status.state=WIFI_STATE_DISABLED;else wifi_link_restore_state(&s_status);
            xSemaphoreGive(s_mutex);
            break;
        }
        case CMD_PAUSE: {
            s_reconnect_allowed = false;
            atomic_store(&s_paused,true);
            wifi_status_t current;wifi_get_status(&current);
            esp_err_t error=current.state==WIFI_STATE_DISABLED?ESP_OK:esp_wifi_stop();
            if(error==ESP_ERR_WIFI_NOT_STARTED || error==ESP_ERR_WIFI_NOT_INIT)error=ESP_OK;
            if(error){atomic_store(&s_paused,false);set_state(WIFI_STATE_FAILED);}
            else {xSemaphoreTake(s_mutex,portMAX_DELAY);s_status.state=WIFI_STATE_DISABLED;s_status.associated=false;s_status.ip[0]=0;xSemaphoreGive(s_mutex);}
            atomic_store(&s_pause_result,error);atomic_store(&s_pause_completed,cmd.pause_id);
            ESP_LOGI(TAG,"Pausa Wi-Fi confirmada: %s",esp_err_to_name(error));
            notify_listener();break;
        }
        case CMD_RESTART_DRIVER:
        case CMD_RESET_C6: {
            /* Recovery is explicit and bounded; never restart the whole P4. */
            s_reconnect_allowed=false;
            set_state(WIFI_STATE_RECOVERING);
            if(s_pending_scan_cb) {
                wifi_scan_cb_t callback=s_pending_scan_cb;void *user=s_pending_scan_user;
                s_pending_scan_cb=NULL;s_pending_scan_user=NULL;
                callback(ESP_ERR_INVALID_STATE,NULL,0,user);
            }
            esp_wifi_scan_stop();
            esp_wifi_stop();
            esp_wifi_deinit();
            esp_err_t recovery=ESP_OK;
            if(cmd.type==CMD_RESET_C6) {
                esp_hosted_deinit();
                recovery=board_c6_power(false);
                vTaskDelay(pdMS_TO_TICKS(300));
                if(recovery==ESP_OK)recovery=board_c6_power(true);
                vTaskDelay(pdMS_TO_TICKS(500));
                while(xSemaphoreTake(s_transport_up_sem,0)==pdTRUE) {}
                if(recovery==ESP_OK)recovery=esp_hosted_init();
                if(recovery==ESP_OK)recovery=esp_hosted_connect_to_slave();
                if(recovery==ESP_OK && xSemaphoreTake(s_transport_up_sem,pdMS_TO_TICKS(10000))!=pdTRUE) recovery=ESP_ERR_TIMEOUT;
            }
            if(recovery==ESP_OK) {
                wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
                config.nvs_enable=false;
                recovery=esp_wifi_init(&config);
                if(recovery==ESP_OK)recovery=esp_wifi_set_mode(WIFI_MODE_STA);
                if(recovery==ESP_OK)recovery=esp_wifi_start();
            }
            xSemaphoreTake(s_mutex,portMAX_DELAY);
            s_status.associated=false;s_status.ip[0]=0;
            snprintf(s_status.error,sizeof(s_status.error),"%s",recovery==ESP_OK?"":esp_err_to_name(recovery));
            s_status.state=recovery==ESP_OK?WIFI_STATE_READY:WIFI_STATE_FAILED;
            xSemaphoreGive(s_mutex);notify_listener();
            break;
        }
        default:
            break;
        }
    }

failed:
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.state = WIFI_STATE_FAILED;
    if (!s_status.error[0])
        snprintf(s_status.error, sizeof(s_status.error), "Wi-Fi initialization failed");
    xSemaphoreGive(s_mutex);
    notify_listener();
    goto idle;
}

/* ---- Public API ---------------------------------------------------------- */

void wifi_init(bool autoconnect)
{
    if (s_initialized) return;
    if (wifi_saved_init() != ESP_OK) return;
    s_autoconnect = autoconnect;
    s_mutex = xSemaphoreCreateMutex();
    s_cmd_q = xQueueCreate(CMD_QUEUE_SIZE, sizeof(wifi_cmd_t));
    s_transport_up_sem = xSemaphoreCreateBinary();
    s_connection_events = xEventGroupCreate();
    if (!s_mutex || !s_cmd_q || !s_transport_up_sem || !s_connection_events) {
        if (s_mutex) vSemaphoreDelete(s_mutex);
        if (s_cmd_q) vQueueDelete(s_cmd_q);
        if (s_transport_up_sem) vSemaphoreDelete(s_transport_up_sem);
        if (s_connection_events) vEventGroupDelete(s_connection_events);
        s_mutex = NULL; s_cmd_q = NULL; s_transport_up_sem = NULL;
        s_connection_events = NULL;
        return;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = WIFI_STATE_DISABLED;
    /* Pin wifi_worker to CPU1 so spi_trans / ESP-Hosted tasks don't starve
     * the LVGL task which runs on CPU0. */
    if (xTaskCreatePinnedToCore(wifi_worker, "wifi_worker", WORKER_STACK, NULL, 4, &s_worker, 1) != pdPASS) {
        vSemaphoreDelete(s_mutex); vQueueDelete(s_cmd_q);
        vSemaphoreDelete(s_transport_up_sem); vEventGroupDelete(s_connection_events);
        s_mutex = NULL; s_cmd_q = NULL; s_transport_up_sem = NULL;
        s_connection_events = NULL;
        return;
    }
    wifi_cmd_t cmd = { .type = CMD_START };
    xQueueSend(s_cmd_q, &cmd, 0);
    s_initialized = true;
}

void wifi_pause(void)
{
    wifi_cmd_t cmd = { .type = CMD_PAUSE };
    if (s_cmd_q) xQueueSend(s_cmd_q, &cmd, pdMS_TO_TICKS(400));
}

esp_err_t wifi_pause_wait(uint32_t timeout_ms){
 if(!s_initialized)return ESP_OK;
 if(!s_cmd_q||atomic_exchange(&s_pause_waiting,true))return ESP_ERR_INVALID_STATE;
 unsigned id=atomic_fetch_add(&s_pause_issued,1)+1;
 wifi_cmd_t cmd={.type=CMD_PAUSE,.pause_id=id};
 esp_err_t result=ESP_ERR_TIMEOUT;
 if(xQueueSend(s_cmd_q,&cmd,pdMS_TO_TICKS(400))!=pdTRUE)result=ESP_ERR_INVALID_STATE;
 else {int64_t deadline=esp_timer_get_time()+(int64_t)timeout_ms*1000;while(esp_timer_get_time()<deadline){if(atomic_load(&s_pause_completed)==id){result=atomic_load(&s_pause_result);break;}vTaskDelay(pdMS_TO_TICKS(25));}}
 atomic_store(&s_pause_waiting,false);return result;
}

void wifi_resume(void)
{
    wifi_cmd_t cmd = { .type = CMD_START };
    if (s_cmd_q) xQueueSend(s_cmd_q, &cmd, 0);
}

void wifi_set_listener(wifi_listener_t cb) { s_listener = cb; }

void wifi_get_status(wifi_status_t *out)
{
    if (!s_mutex) { memset(out, 0, sizeof(*out)); return; }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_mutex);
}

esp_err_t wifi_scan(wifi_scan_cb_t cb, void *user)
{
    wifi_status_t status;
    wifi_get_status(&status);
    if (status.state != WIFI_STATE_READY && status.state != WIFI_STATE_CONNECTED)
        return ESP_ERR_INVALID_STATE;
    wifi_cmd_t cmd = { .type = CMD_SCAN, .scan_cb = cb, .user = user };
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_connect(const char *ssid, const char *pass, uint8_t auth,
                       wifi_connect_cb_t cb, void *user)
{
    if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32 ||
        (pass && strlen(pass) > 63) || !wifi_auth_supported(auth)) return ESP_ERR_INVALID_ARG;
    wifi_status_t current;
    wifi_get_status(&current);
    if (current.state != WIFI_STATE_READY && current.state != WIFI_STATE_CONNECTED)
        return ESP_ERR_INVALID_STATE;
    wifi_cmd_t cmd = { .type = CMD_CONNECT, .auth = auth, .conn_cb = cb, .user = user };
    strncpy(cmd.ssid, ssid, sizeof(cmd.ssid) - 1);
    if (pass) strncpy(cmd.pass, pass, sizeof(cmd.pass) - 1);
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_disconnect(void)
{
    wifi_cmd_t cmd = { .type = CMD_DISCONNECT };
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_refresh_signal(void)
{
    wifi_cmd_t cmd = { .type = CMD_REFRESH_SIGNAL };
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void wifi_retry(void)   { wifi_cmd_t c = {.type=CMD_RETRY};   if (s_cmd_q) xQueueSend(s_cmd_q, &c, 0); }
void wifi_reconnect(void){ wifi_cmd_t c = {.type=CMD_RECONNECT}; if (s_cmd_q) xQueueSend(s_cmd_q, &c, 0); }

esp_err_t wifi_restart_driver(void)
{
    wifi_cmd_t cmd = { .type = CMD_RESTART_DRIVER };
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t wifi_reset_c6(void)
{
    wifi_cmd_t cmd = { .type = CMD_RESET_C6 };
    return s_cmd_q && xQueueSend(s_cmd_q, &cmd, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

const char *wifi_diagnostics(void)
{
    static char buf[512];
    wifi_status_t st;
    wifi_get_status(&st);
    snprintf(buf, sizeof(buf),
             "State: %d\nSSID: %s\nIP: %s\nRSSI: %d dBm\nHost: %s / C6: %s / series diferente: %s\nError: %s",
             st.state, st.ssid, st.ip, st.rssi, wifi_host_version(), st.c6_fw,st.c6_mismatch?"sim":"nao",st.error);
    return buf;
}

#define VERSION_STRING_INNER(x) #x
#define VERSION_STRING(x) VERSION_STRING_INNER(x)
const char *wifi_host_version(void) {return VERSION_STRING(ESP_HOSTED_VERSION_MAJOR_1) "." VERSION_STRING(ESP_HOSTED_VERSION_MINOR_1) "." VERSION_STRING(ESP_HOSTED_VERSION_PATCH_1);}
const char *wifi_host_series(void) {return VERSION_STRING(ESP_HOSTED_VERSION_MAJOR_1) "." VERSION_STRING(ESP_HOSTED_VERSION_MINOR_1);}

const char *wifi_auth_name(uint8_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN:          return "OPEN";
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    case WIFI_AUTH_ENTERPRISE: return "Enterprise";
    default:                      return "UNKNOWN";
    }
}

bool wifi_auth_needs_password(uint8_t auth) { return auth != WIFI_AUTH_OPEN; }
bool wifi_auth_supported(uint8_t auth)
{
    return auth == WIFI_AUTH_OPEN || auth == WIFI_AUTH_WPA_PSK ||
           auth == WIFI_AUTH_WPA2_PSK || auth == WIFI_AUTH_WPA_WPA2_PSK ||
           auth == WIFI_AUTH_WPA3_PSK || auth == WIFI_AUTH_WPA2_WPA3_PSK;
}

bool wifi_password_too_short(uint8_t auth, const char *pass)
{
    if (!wifi_auth_needs_password(auth)) return false;
    return pass == NULL || strlen(pass) < 8;
}
