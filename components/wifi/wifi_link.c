/* Connection sequencing shared by the firmware and host event-injection tests.
 */
#include "wifi_link.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "wifi_link";
static atomic_bool s_cancel;
void wifi_link_cancel(bool cancel) { atomic_store(&s_cancel, cancel); }
static EventBits_t wait_connection(EventGroupHandle_t events) {
  for (unsigned elapsed = 0; elapsed < 20000 && !atomic_load(&s_cancel); elapsed += 100) {
    EventBits_t bits = xEventGroupWaitBits(events,
        WIFI_LINK_CONNECTED | WIFI_LINK_DISCONNECTED, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(100));
    if (bits) return bits;
  }
  return 0;
}

void wifi_link_restore_state(wifi_status_t *status) {
  status->state = status->associated && status->ip[0] ? WIFI_STATE_CONNECTED
                                                      : WIFI_STATE_READY;
}
static bool disconnect_and_wait(wifi_status_t *status, SemaphoreHandle_t lock,
                                EventGroupHandle_t events) {
  xEventGroupClearBits(events, WIFI_LINK_CONNECTED | WIFI_LINK_DISCONNECTED);
  esp_err_t err = esp_wifi_disconnect();
  if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT)
    return false;
  if (err == ESP_OK) {
    EventBits_t bits = xEventGroupWaitBits(
        events, WIFI_LINK_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    if (!(bits & WIFI_LINK_DISCONNECTED))
      return false;
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  status->associated = false;
  status->ip[0] = 0;
  status->state = WIFI_STATE_READY;
  xSemaphoreGive(lock);
  xEventGroupClearBits(events, WIFI_LINK_CONNECTED | WIFI_LINK_DISCONNECTED);
  return true;
}
wifi_result_t wifi_link_connect(const wifi_saved_network_t *network,
                                wifi_status_t *status, SemaphoreHandle_t lock,
                                EventGroupHandle_t events, char *detail,
                                size_t detail_size) {
  if (detail_size)
    detail[0] = 0;
  if (atomic_load(&s_cancel)) return WIFI_RESULT_ERR_TRANSPORT;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool connected = status->associated && status->ip[0];
  bool same = connected && strcmp(status->ssid, network->ssid) == 0;
  bool busy = status->associated ||
              status->state == WIFI_STATE_AUTHENTICATING ||
              status->state == WIFI_STATE_GETTING_IP;
  xSemaphoreGive(lock);
  /* Selecting the connected network must not tear it down or save untested
   * credentials. */
  if (same)
    return WIFI_RESULT_OK;
  if (busy && !disconnect_and_wait(status, lock, events)) {
    snprintf(detail, detail_size,
             "Sem confirmacao da desconexao anterior. Tente novamente.");
    return WIFI_RESULT_ERR_TRANSPORT;
  }

  xEventGroupClearBits(events, WIFI_LINK_CONNECTED | WIFI_LINK_DISCONNECTED);
  xSemaphoreTake(lock, portMAX_DELAY);
  status->state = WIFI_STATE_AUTHENTICATING;
  status->associated = false;
  status->ip[0] = 0;
  status->error[0] = 0;
  status->disconnect_reason = 0;
  status->network_saved = false;
  memcpy(status->ssid, network->ssid, sizeof(status->ssid));
  xSemaphoreGive(lock);

  wifi_config_t config = {0};
  memcpy(config.sta.ssid, network->ssid, sizeof(config.sta.ssid));
  memcpy(config.sta.password, network->password, sizeof(config.sta.password));
  config.sta.threshold.authmode = network->auth == WIFI_AUTH_WPA2_WPA3_PSK
                                      ? WIFI_AUTH_WPA2_PSK
                                      : network->auth;
  config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err == ESP_OK)
    err = esp_wifi_connect();
  EventBits_t bits = 0;
  if (err == ESP_OK)
    bits = wait_connection(events);
  if (atomic_load(&s_cancel)) {
    snprintf(detail, detail_size, "Conexao interrompida para iniciar aplicativo.");
    return WIFI_RESULT_ERR_TRANSPORT;
  }

  xSemaphoreTake(lock, portMAX_DELAY);
  bool got_ip = status->state == WIFI_STATE_CONNECTED && status->associated &&
                status->ip[0];
  uint16_t reason = status->disconnect_reason;
  xSemaphoreGive(lock);
  if (err == ESP_OK && got_ip) {
    esp_err_t saved = wifi_store_put(network);
    wifi_ap_record_t ap;
    bool have_ap = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    xSemaphoreTake(lock, portMAX_DELAY);
    status->network_saved = saved == ESP_OK;
    if (have_ap)
      status->rssi = ap.rssi;
    xSemaphoreGive(lock);
    if (saved == ESP_OK)
      ESP_LOGI(TAG, "connected; credentials saved on microSD");
    else {
      ESP_LOGE(TAG, "connected but credential save failed: %s",
               esp_err_to_name(saved));
      snprintf(detail, detail_size,
               "Conectado, mas nao foi possivel salvar a rede.");
    }
    return WIFI_RESULT_OK;
  }
  if (err != ESP_OK) {
    snprintf(detail, detail_size, "Falha do driver: %s", esp_err_to_name(err));
  } else if (bits & WIFI_LINK_DISCONNECTED) {
    snprintf(detail, detail_size,
             "AP desconectou (motivo %u). Verifique sinal e credenciais.",
             reason);
  } else {
    snprintf(detail, detail_size, "Tempo esgotado aguardando IP (DHCP).");
    /* Timeout cleanup is acknowledged before a new request can start. */
    if (!disconnect_and_wait(status, lock, events)) {
      xSemaphoreTake(lock, portMAX_DELAY);
      status->state = WIFI_STATE_FAILED;
      snprintf(status->error, sizeof(status->error),
               "Timeout e desconexao nao confirmada");
      xSemaphoreGive(lock);
      return WIFI_RESULT_ERR_TRANSPORT;
    }
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  wifi_link_restore_state(status);
  snprintf(status->error, sizeof(status->error), "%s", detail);
  xSemaphoreGive(lock);
  if (err != ESP_OK)
    return WIFI_RESULT_ERR_C6;
  return bits & WIFI_LINK_DISCONNECTED ? WIFI_RESULT_ERR_REFUSED
                                       : WIFI_RESULT_ERR_TIMEOUT;
}

wifi_result_t wifi_link_connect_retry(const wifi_saved_network_t *network,
                                      wifi_status_t *status,
                                      SemaphoreHandle_t lock,
                                      EventGroupHandle_t events, char *detail,
                                      size_t detail_size) {
  static const unsigned backoff_ms[] = {10000, 20000};
  wifi_result_t result;
  for (unsigned attempt = 0;; ++attempt) {
    result =
        wifi_link_connect(network, status, lock, events, detail, detail_size);
    xSemaphoreTake(lock, portMAX_DELAY);
    bool transient = result == WIFI_RESULT_ERR_REFUSED &&
                     status->disconnect_reason == WIFI_REASON_AUTH_EXPIRE &&
                     !status->associated && status->state == WIFI_STATE_READY;
    if (transient && attempt < 2) {
      status->state = WIFI_STATE_RECONNECTING;
      snprintf(status->error, sizeof(status->error),
               "Autenticacao expirou. Tentativa %u/3 em %u s.", attempt + 2,
               backoff_ms[attempt] / 1000);
    }
    xSemaphoreGive(lock);
    if (!transient || attempt >= 2)
      return result;
    ESP_LOGW(TAG, "authentication expired (reason 2), retry %u/3 in %u ms",
             attempt + 2, backoff_ms[attempt]);
    for (unsigned elapsed = 0; elapsed < backoff_ms[attempt]; elapsed += 100) {
      if (atomic_load(&s_cancel)) return WIFI_RESULT_ERR_TRANSPORT;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void wifi_link_disconnected(wifi_status_t *status, unsigned reason,
                            bool paused) {
  status->disconnect_reason = reason;
  status->associated = false;
  status->ip[0] = 0;
  status->state = paused ? WIFI_STATE_DISABLED : WIFI_STATE_READY;
}
