/* Independent implementation of Tab5Link HELLO/STAT/CALL/REPLY/PART/SHUTDOWN.
 * Wire contract: UNIZERSION tab5-launcher da5aa984, main/l5_pi_proto.h.
 * Optional USB-A host. USB Serial/JTAG maintenance uses a different controller.
 */
#include "pi_link.h"
#include "usb_host_bus.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
#include "board.h"
#include "bsp/m5stack_tab5.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "time_mgr.h"
#include "usb/cdc_acm_host.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pi_link_status_t s_status;
static SemaphoreHandle_t s_lock, s_call_lock, s_reply;
static QueueHandle_t s_requests;
static _Atomic bool s_disconnected;
static cdc_acm_dev_hdl_t s_device;
static EXT_RAM_BSS_ATTR char s_line[256], s_meta[384];
static unsigned s_line_length;
static bool s_bad_line;
static uint32_t s_id, s_next_id, s_raw_left, s_total, s_received;
static unsigned s_meta_length, s_meta_received;
static bool s_raw_meta, s_done, s_failed;
static int s_code;
static uint8_t *s_body;
static size_t s_capacity;
static int64_t s_last_rx;
typedef struct {
  char service[32];
  const void *request;
  size_t length;
  bool shutdown;
  esp_err_t error;
  SemaphoreHandle_t done;
} request_t;
typedef struct {
  uint32_t id;
  char command[24], arg[1025];
  unsigned used, total;
} ask_t;
static EXT_RAM_BSS_ATTR ask_t incoming_ask;
static bool raw_ask;
static QueueHandle_t asks;
static pi_ask_handler_fn ask_handler;
void pi_link_set_ask_handler(pi_ask_handler_fn fn) { ask_handler = fn; }
static void queue_ask(void) {
  incoming_ask.arg[incoming_ask.used] = 0;
  if (!asks || xQueueSend(asks, &incoming_ask, 0) != pdTRUE)
    atomic_store(&s_disconnected, true);
  memset(&incoming_ask, 0, sizeof(incoming_ask));
  raw_ask = false;
}
static void complete(bool failed) {
  s_failed = failed;
  s_done = true;
  xSemaphoreGive(s_reply);
}
static void line(void) {
  s_line[s_line_length] = 0;
  char host[32], version[16];
  if (sscanf(s_line, "HELLO %31s %15s", host, version) == 2) {
    snprintf(s_status.host, sizeof(s_status.host), "%s", host);
    snprintf(s_status.version, sizeof(s_status.version), "%s", version);
    s_status.online = true;
    s_status.bye = false;
    s_status.error[0] = 0;
  } else if (!strncmp(s_line, "STAT ", 5)) {
    const char *ip = strstr(s_line, "ip=");
    if (ip) {
      size_t n = strcspn(ip + 3, " \r\n");
      if (n < sizeof(s_status.ip)) {
        memcpy(s_status.ip, ip + 3, n);
        s_status.ip[n] = 0;
      }
    }
  } else if (!strcmp(s_line, "BYE")) {
    s_status.bye = true;
    s_status.online = false;
  } else if (!strncmp(s_line, "REPLY ", 6)) {
    unsigned id, metadata, body;
    int status, used = 0;
    if (sscanf(s_line, "REPLY %u %d %u %u%n", &id, &status, &metadata, &body,
               &used) != 4 ||
        s_line[used] || id != s_id || !s_id || metadata > 383 ||
        body > s_capacity) {
      complete(true);
      atomic_store(&s_disconnected, true);
      return;
    }
    s_code = status;
    s_meta_length = metadata;
    s_meta_received = 0;
    s_received = 0;
    s_total = body;
    s_raw_meta = true;
    s_raw_left = metadata;
    if (!metadata && !body)
      complete(false);
  } else if (!strncmp(s_line, "PART ", 5)) {
    unsigned id, count;
    int used = 0;
    if (sscanf(s_line, "PART %u %u%n", &id, &count, &used) != 2 ||
        s_line[used] || id != s_id || !s_id ||
        s_meta_received != s_meta_length || !count ||
        count > s_total - s_received) {
      complete(true);
      atomic_store(&s_disconnected, true);
      return;
    }
    s_raw_meta = false;
    s_raw_left = count;
  } else if (!strncmp(s_line, "ASK ", 4)) {
    unsigned id, n;
    int used = 0;
    char command[24];
    if (sscanf(s_line, "ASK %u %23s %u%n", &id, command, &n, &used) != 3 ||
        s_line[used] || !id || n > 1024 || raw_ask) {
      atomic_store(&s_disconnected, true);
      return;
    }
    memset(&incoming_ask, 0, sizeof(incoming_ask));
    incoming_ask.id = id;
    incoming_ask.total = n;
    snprintf(incoming_ask.command, sizeof(incoming_ask.command), "%s", command);
    raw_ask = n > 0;
    if (!n)
      queue_ask();
  }
}
static bool received(const uint8_t *data, size_t length, void *arg) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_last_rx = esp_timer_get_time();
  for (size_t i = 0; i < length; i++) {
    if (raw_ask) {
      if (!data[i]) {
        atomic_store(&s_disconnected, true);
        raw_ask = false;
        break;
      }
      incoming_ask.arg[incoming_ask.used++] = data[i];
      if (incoming_ask.used == incoming_ask.total)
        queue_ask();
      continue;
    }
    if (s_raw_left) {
      if (s_raw_meta)
        s_meta[s_meta_received++] = (char)data[i];
      else
        s_body[s_received++] = data[i];
      if (--s_raw_left == 0) {
        if (s_raw_meta)
          s_meta[s_meta_received] = 0;
        if (s_received == s_total && s_meta_received == s_meta_length)
          complete(false);
      }
      continue;
    }
    if (data[i] == '\n') {
      if (!s_bad_line)
        line();
      s_line_length = 0;
      s_bad_line = false;
    } else if (data[i] != '\r') {
      if (s_line_length < sizeof(s_line) - 1)
        s_line[s_line_length++] = (char)data[i];
      else
        s_bad_line = true;
    }
  }
  xSemaphoreGive(s_lock);
  return true;
}
static void event(const cdc_acm_host_dev_event_data_t *e, void *arg) {
  if (e->type == CDC_ACM_HOST_DEVICE_DISCONNECTED ||
      e->type == CDC_ACM_HOST_ERROR) {
    atomic_store(&s_disconnected, true);
    xSemaphoreGive(s_reply);
  }
}
static esp_err_t send(const void *bytes, size_t length) {
  return s_device ? cdc_acm_host_data_tx_blocking(s_device, bytes, length, 3000)
                  : ESP_ERR_INVALID_STATE;
}
static void worker(void *arg) {
  request_t *request;
  ask_t ask;
  int64_t hello_at = 0;
  for (;;) {
    if (atomic_exchange(&s_disconnected, false)) {
      if (s_device)
        cdc_acm_host_close(s_device);
      s_device = NULL;
      if (asks)
        while (xQueueReceive(asks, &ask, 0) == pdTRUE) {
        }
      xSemaphoreTake(s_lock, portMAX_DELAY);
      s_status.usb = s_status.online = false;
      s_raw_left = s_line_length = 0;
      raw_ask = false;
      memset(&incoming_ask, 0, sizeof(incoming_ask));
      s_id = 0;
      xSemaphoreGive(s_lock);
    }
    if (!s_device) {
      cdc_acm_host_device_config_t c = {.connection_timeout_ms = 500,
                                        .out_buffer_size = 4096,
                                        .in_buffer_size = 4096,
                                        .event_cb = event,
                                        .data_cb = received};
      if (cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, &c,
                            &s_device) == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.usb = true;
        s_last_rx = esp_timer_get_time();
        xSemaphoreGive(s_lock);
        cdc_acm_host_set_control_line_state(s_device, true, true);
        hello_at = 0;
      }
    }
    int64_t now = esp_timer_get_time();
    if (s_device && now >= hello_at) {
      send("HELLO?\n", 7);
      hello_at = now + 5000000;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (now - s_last_rx > 10000000)
      s_status.online = false;
    xSemaphoreGive(s_lock);
    if (asks && xQueueReceive(asks, &ask, 0) == pdTRUE) {
      uint8_t *body = NULL;
      size_t length = 0;
      int code = 404;
      if (ask_handler && s_status.online)
        ask_handler(ask.command, ask.arg, &body, &length, &code);
      if (length > 4 * 1048576 || (!body && length)) {
        free(body);
        body = NULL;
        length = 0;
        code = 500;
      }
      char answer[80];
      snprintf(answer, sizeof(answer), "ANSWER %lu %d %u\n",
               (unsigned long)ask.id, code, (unsigned)length);
      esp_err_t e = send(answer, strlen(answer));
      for (size_t off = 0; !e && off < length;) {
        size_t n = length - off < 4096 ? length - off : 4096;
        e = send(body + off, n);
        off += n;
      }
      free(body);
      if (e)
        atomic_store(&s_disconnected, true);
    }
    if (xQueueReceive(s_requests, &request, pdMS_TO_TICKS(250)) != pdTRUE)
      continue;
    request->error = ESP_ERR_INVALID_STATE;
    if (s_device) {
      if (request->shutdown)
        request->error = send("SHUTDOWN\n", 9);
      else {
        char header[96];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_id = ++s_next_id;
        if (!s_id)
          s_id = ++s_next_id;
        s_done = s_failed = false;
        s_received = s_total = s_meta_length = s_meta_received = s_raw_left = 0;
        s_meta[0] = 0;
        xSemaphoreGive(s_lock);
        while (xSemaphoreTake(s_reply, 0) == pdTRUE) {
        }
        snprintf(header, sizeof(header), "CALL %lu %s %u\n",
                 (unsigned long)s_id, request->service,
                 (unsigned)request->length);
        request->error = send(header, strlen(header));
        if (!request->error && request->length)
          request->error = send(request->request, request->length);
        if (!request->error &&
            xSemaphoreTake(s_reply, pdMS_TO_TICKS(30000)) != pdTRUE)
          request->error = ESP_ERR_TIMEOUT;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!request->error && (!s_done || s_failed))
          request->error = ESP_FAIL;
        /* Drop timed-out transport: late raw payload must never outlive caller
         * buffers. */
        if (request->error)
          atomic_store(&s_disconnected, true);
        s_id = 0;
        s_raw_left = 0;
        s_body = NULL;
        s_capacity = 0;
        xSemaphoreGive(s_lock);
      }
    }
    xSemaphoreGive(request->done);
  }
}
esp_err_t pi_link_init(void) {
  if (s_status.started)
    return ESP_OK;
  if (s_requests)
    return ESP_ERR_INVALID_STATE;
  s_lock = xSemaphoreCreateMutex();
  s_call_lock = xSemaphoreCreateMutex();
  s_reply = xSemaphoreCreateBinary();
  s_requests = xQueueCreate(1, sizeof(request_t *));
  asks = xQueueCreate(2, sizeof(ask_t));
  if (!s_lock || !s_call_lock || !s_reply || !s_requests || !asks)
    return ESP_ERR_NO_MEM;
  esp_err_t e=usb_host_bus_start();
  if (!e)
    e = cdc_acm_host_install(NULL);
  if (e) {
    snprintf(s_status.error, sizeof(s_status.error), "USB host: %s",
             esp_err_to_name(e));
    return e;
  }
  if (xTaskCreate(worker, "pi_link", 8192, NULL, 3, NULL) != pdPASS)
    return ESP_ERR_NO_MEM;
  s_status.started = true;
  return ESP_OK;
}
void pi_link_status(pi_link_status_t *out) {
  if (!out)
    return;
  if (s_lock)
    xSemaphoreTake(s_lock, portMAX_DELAY);
  *out = s_status;
  if (s_lock)
    xSemaphoreGive(s_lock);
}
esp_err_t pi_link_call(const char *service, const void *data, size_t length,
                       void *body, size_t capacity, size_t *count, int *status,
                       char *meta, size_t meta_size) {
  if (!s_status.started || !service || !*service || strlen(service) >= 32 ||
      length > 65536 || capacity > 262144 || (!data && length) ||
      (!body && capacity))
    return ESP_ERR_INVALID_ARG;
  for (const char *p = service; *p; p++)
    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
      return ESP_ERR_INVALID_ARG;
  if (xSemaphoreTake(s_call_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  request_t r = {
      .request = data, .length = length, .done = xSemaphoreCreateBinary()};
  snprintf(r.service, sizeof(r.service), "%s", service);
  if (!r.done) {
    xSemaphoreGive(s_call_lock);
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_body = body;
  s_capacity = capacity;
  xSemaphoreGive(s_lock);
  request_t *ptr = &r;
  if (xQueueSend(s_requests, &ptr, 0) == pdTRUE) {
    xSemaphoreTake(r.done, portMAX_DELAY);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (count)
      *count = s_received;
    if (status)
      *status = s_code;
    if (meta && meta_size)
      snprintf(meta, meta_size, "%s", s_meta);
    xSemaphoreGive(s_lock);
  } else
    r.error = ESP_ERR_INVALID_STATE;
  vSemaphoreDelete(r.done);
  xSemaphoreGive(s_call_lock);
  return r.error;
}
esp_err_t pi_link_shutdown(void) {
  if (!s_status.started || xSemaphoreTake(s_call_lock, 0) != pdTRUE)
    return ESP_ERR_INVALID_STATE;
  request_t r = {.shutdown = true, .done = xSemaphoreCreateBinary()};
  if (!r.done) {
    xSemaphoreGive(s_call_lock);
    return ESP_ERR_NO_MEM;
  }
  request_t *ptr = &r;
  if (xQueueSend(s_requests, &ptr, 0) == pdTRUE)
    xSemaphoreTake(r.done, portMAX_DELAY);
  else
    r.error = ESP_ERR_INVALID_STATE;
  vSemaphoreDelete(r.done);
  xSemaphoreGive(s_call_lock);
  return r.error;
}
esp_err_t pi_link_sync_time(void) {
  char body[256] = {0}, meta[384];
  size_t count = 0;
  int status = 0;
  esp_err_t e = pi_link_call("time", NULL, 0, body, sizeof(body) - 1, &count,
                             &status, meta, sizeof(meta));
  if (e || status != 200)
    return e ? e : ESP_FAIL;
  /* Reference service body is ASCII Unix epoch. Reject ambiguity and overflow.
   */
  char *end;
  double epoch = strtod(body, &end);
  while (*end == '\n' || *end == '\r' || *end == ' ')
    end++;
  if (end == body || !isfinite(epoch) || *end || epoch < 1704067200 ||
      epoch > 4102444799LL)
    return ESP_ERR_INVALID_ARG;
  return time_set_manual((time_t)epoch);
}
