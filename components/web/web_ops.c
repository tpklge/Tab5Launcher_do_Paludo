#include "web_ops.h"
#include "board.h"
#include "boot_mgr.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "flash_snapshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "installer.h"
#include "lvgl.h"
#include "pi_services.h"
#include "web_security.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  char action[24], path[512], ticket[17];
  uint8_t digest[32];
  uint32_t sizes[16];
  unsigned count;
  int slot;
  uint32_t data_mask;
  bool backup_binary;
  install_plan_t plan;
  flash_snapshot_t snapshot;
} operation_t;
static operation_t *pending;
static QueueHandle_t operations;
static TaskHandle_t operation_task;
static atomic_bool running;
static SemaphoreHandle_t lock, request_lock;
static unsigned percent;
static int result;
static char stage[600];
static cJSON *reply(bool ok, const char *message) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "ok", ok);
  cJSON_AddStringToObject(j, "message", message);
  return j;
}
bool web_ops_busy(void) { return atomic_load(&running); }
static bool progress(unsigned p, const char *s, void *u) {
  xSemaphoreTake(lock, portMAX_DELAY);
  percent = p;
  snprintf(stage, sizeof(stage), "%s", s);
  xSemaphoreGive(lock);
  return true;
}
static bool restarts(const operation_t *o) {
  return !strcmp(o->action, "layout") || !strcmp(o->action, "compact") ||
         !strcmp(o->action, "resume") || !strcmp(o->action, "restore") ||
         (!strcmp(o->action, "install") && o->plan.needs_layout);
}
static void worker(void *arg) {
  operation_t *o = arg;
  /* Let the initiating HTTP response leave TCP before pausing Wi-Fi. */
  if (restarts(o))
    vTaskDelay(pdMS_TO_TICKS(1000));
  char message[600] = {0};
  esp_err_t e = ESP_ERR_INVALID_ARG;
  bool reboot = false;
  if (!strcmp(o->action, "compact")) {
    e = installer_compact(o->digest, progress, NULL, message, sizeof(message));
    reboot = !e;
  }
  if (!strcmp(o->action, "layout")) {
    e = installer_layout_apply(o->sizes, o->digest, progress, NULL, message,
                               sizeof(message));
    reboot = !e;
  }
  if (!strcmp(o->action, "install"))
    e = installer_execute(&o->plan, progress, NULL, message, sizeof(message));
  else if (!strcmp(o->action, "uninstall"))
    e = installer_uninstall_options(o->slot, o->digest, o->backup_binary,
                                    o->data_mask, progress, NULL, message,
                                    sizeof(message));
  else if (!strcmp(o->action, "migrate")) {
    e = installer_repartition(o->sizes, o->count, o->digest, progress, NULL,
                              message, sizeof(message));
    reboot = !e;
  } else if (!strcmp(o->action, "backup")) {
    e = installer_full_backup(&o->snapshot, progress, NULL, message,
                              sizeof(message));
  } else if (!strcmp(o->action, "restore")) {
    e = installer_full_restore(&o->snapshot, progress, NULL, message,
                               sizeof(message));
    reboot = !e;
  } else if (!strcmp(o->action, "resume")) {
    e = installer_resume_flash(progress, NULL, message, sizeof(message));
    reboot = !e;
  }
  if (!strcmp(o->action, "pi-backup")) {
    e = installer_full_backup(&o->snapshot, progress, NULL, message,
                              sizeof(message));
    if (!e)
      e = pi_services_backup(o->snapshot.path, progress, NULL);
    if (!e)
      snprintf(message, sizeof(message),
               "Backup no Pi relido e verificado; copia local preservada.");
  }
  if (!strcmp(o->action, "pi-fetch") || !strcmp(o->action, "pi-install")) {
    char local[512];
    e = pi_services_fetch(o->sizes[0] ? "backups" : "apps", o->path, local,
                          sizeof(local), progress, NULL);
    if (!e && !strcmp(o->action, "pi-install")) {
      e = installer_plan(local, &o->plan, message, sizeof(message));
      if (!e &&
          (o->plan.slot != o->slot || o->plan.firmware.kind != FW_APP_ONLY))
        e = ESP_ERR_NOT_SUPPORTED;
      if (!e)
        e = installer_execute(&o->plan, progress, NULL, message,
                              sizeof(message));
    } else if (!e)
      snprintf(message, sizeof(message), "Arquivo recebido e verificado: %s",
               local);
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  result = e;
  percent = e ? percent : 100;
  snprintf(stage, sizeof(stage), "%.560s%s",
           message[0] ? message : esp_err_to_name(e),
           reboot ? " Reiniciando em 5 segundos." : "");
  xSemaphoreGive(lock);
  free(o);
  if (!reboot)
    atomic_store(&running, false);
  if (reboot) {
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
}
static void operation_loop(void *unused) {
  operation_t *o;
  for (;;)
    if (xQueueReceive(operations, &o, portMAX_DELAY) == pdTRUE)
      worker(o);
}
cJSON *web_ops_status(void) {
  if (!lock)
    lock = xSemaphoreCreateMutex();
  if (!lock)
    return reply(false, "Sem memoria");
  cJSON *j = cJSON_CreateObject();
  xSemaphoreTake(lock, portMAX_DELAY);
  cJSON_AddNumberToObject(j, "percent", percent);
  cJSON_AddNumberToObject(j, "error", result);
  cJSON_AddStringToObject(j, "message", stage);
  cJSON_AddBoolToObject(j, "running", web_ops_busy());
  xSemaphoreGive(lock);
  return j;
}
static cJSON *request_locked(const cJSON *j) {
  if (!lock)
    lock = xSemaphoreCreateMutex();
  if (!lock)
    return reply(false, "Sem memoria");
  if (web_ops_busy() || installer_is_busy())
    return reply(false, "Operacao em andamento");
  const cJSON *action = cJSON_GetObjectItemCaseSensitive(j, "action"),
              *ticket = cJSON_GetObjectItemCaseSensitive(j, "ticket");
  if (!cJSON_IsString(action))
    return reply(false, "Acao invalida");
  if (!strcmp(action->valuestring, "execute")) {
    if (!pending || !cJSON_IsString(ticket) ||
        strcmp(ticket->valuestring, pending->ticket))
      return reply(false, "Plano expirado. Analise novamente.");
    operation_t *o = pending;
    bool will_restart = restarts(o);
    pending = NULL;
    atomic_store(&running, true);
    progress(0, "Iniciando operacao", NULL);
    result = 0;
    if (!operation_task || xQueueSend(operations, &o, 0) != pdTRUE) {
      free(o);
      atomic_store(&running, false);
      return reply(false, "Sem memoria para operacao");
    }
    cJSON *r = reply(true, "Operacao iniciada");
    cJSON_AddBoolToObject(r, "willRestart", will_restart);
    return r;
  }
  free(pending);
  pending = NULL;
  operation_t *o = calloc(1, sizeof(*o));
  if (!o)
    return reply(false, "Sem memoria");
  snprintf(o->action, sizeof(o->action), "%s", action->valuestring);
  char message[600] = {0};
  esp_err_t e = ESP_ERR_INVALID_ARG;
  const cJSON *path = cJSON_GetObjectItemCaseSensitive(j, "path"),
              *slot = cJSON_GetObjectItemCaseSensitive(j, "slot");
  if (cJSON_IsString(path) && sd_path_valid(path->valuestring) &&
      !web_path_is_private(path->valuestring))
    snprintf(o->path, sizeof(o->path), "%s", path->valuestring);
  if (!strcmp(o->action, "compact")) {
    flash_allocation_t allocation;
    e = flash_allocate_plan(0, &allocation, message, sizeof(message));
    if (!e)
      memcpy(o->digest, allocation.digest, 32);
  }
  if (!strcmp(o->action, "layout")) {
    const cJSON *sizes = cJSON_GetObjectItemCaseSensitive(j, "sizes");
    if (cJSON_IsArray(sizes) && cJSON_GetArraySize(sizes) == 16) {
      e = ESP_OK;
      for (unsigned i = 0; i < 16; i++) {
        const cJSON *v = cJSON_GetArrayItem(sizes, i);
        if (!cJSON_IsNumber(v) || v->valuedouble < 0 ||
            v->valuedouble > 0xa00000 ||
            v->valuedouble != (uint32_t)v->valuedouble) {
          e = ESP_ERR_INVALID_ARG;
          break;
        }
        o->sizes[i] = (uint32_t)v->valuedouble;
      }
      flash_allocation_t layout;
      if (!e)
        e = flash_layout_plan(o->sizes, &layout, message, sizeof(message));
      if (!e)
        memcpy(o->digest, layout.digest, 32);
    }
  }
  if (!strcmp(o->action, "install")) {
    e = installer_plan(o->path, &o->plan, message, sizeof(message));
    if (!e)
      snprintf(
          message, sizeof(message),
          "Instalar %s no slot %d (%s). %s Os dados exigidos pelo firmware "
          "podem ser compartilhados; confira as particoes antes de confirmar.",
          o->plan.firmware.image.project, o->plan.slot, o->plan.target,
          o->plan.needs_layout
              ? "Reorganiza a flash e reinicia. Confirme a instalacao na tela "
                "SD apos reiniciar."
              : "Espaco disponivel; apps existentes preservados.");
  } else if (!strcmp(o->action, "uninstall") && cJSON_IsNumber(slot) &&
             slot->valuedouble == (int)slot->valuedouble) {
    o->slot = slot->valueint;
    const cJSON *binary = cJSON_GetObjectItemCaseSensitive(j, "backupBinary");
    o->backup_binary = !cJSON_IsFalse(binary);
    e = installer_slot_digest(o->slot, o->digest);
    if (!e) {
      installer_data_option_t opts[INSTALLER_DATA_OPTIONS];
      unsigned count = installer_data_options(o->slot, opts);
      const cJSON *backup = cJSON_GetObjectItemCaseSensitive(j, "backupData");
      if (count && cJSON_IsTrue(backup))
        o->data_mask = (1u << count) - 1;
      snprintf(message, sizeof(message),
               "Remover app %d %s backup do binario. %u particoes de dados "
               "disponiveis; %s. Dados externos serao preservados.",
               o->slot, o->backup_binary ? "com" : "sem", count,
               o->data_mask ? "backup de dados selecionado"
                            : "sem backup de dados");
    }
  } else if (!strcmp(o->action, "migrate")) {
    const cJSON *sizes = cJSON_GetObjectItemCaseSensitive(j, "sizes");
    o->count = cJSON_GetArraySize(sizes);
    if (cJSON_IsArray(sizes) && o->count >= 1 && o->count <= 4) {
      e = ESP_OK;
      for (unsigned i = 0; i < o->count; i++) {
        cJSON *s = cJSON_GetArrayItem(sizes, i);
        if (!cJSON_IsNumber(s) || s->valuedouble != s->valueint ||
            s->valueint < 4 || s->valueint > 32) {
          e = ESP_ERR_INVALID_ARG;
          break;
        }
        o->sizes[i] = s->valueint * 0x40000;
      }
      if (!e)
        e = flash_migrate_plan(o->sizes, o->count, o->digest, message,
                               sizeof(message));
    }
  } else if (!strcmp(o->action, "restore"))
    e = flash_snapshot_validate(o->path, &o->snapshot, message,
                                sizeof(message));
  else if (!strcmp(o->action, "backup")) {
    e = ESP_OK;
    snprintf(message, sizeof(message),
             "Criar backup integral verificado no SD (16 MiB).");
  } else if (!strcmp(o->action, "resume")) {
    e = installer_recovery_required() ? ESP_OK : ESP_ERR_INVALID_STATE;
    snprintf(message, sizeof(message),
             "Retomar operacao interrompida a partir do backup aprovado.");
  }
  if (!strcmp(o->action, "pi-backup")) {
    e = ESP_OK;
    snprintf(
        message, sizeof(message),
        "Criar backup integral local, enviar ao Pi e verificar por releitura.");
  }
  if (!strcmp(o->action, "pi-fetch") || !strcmp(o->action, "pi-install")) {
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name"),
                *backups = cJSON_GetObjectItemCaseSensitive(j, "backups");
    if (cJSON_IsString(name) && sd_name_valid(name->valuestring) &&
        strlen(name->valuestring) <= 240) {
      snprintf(o->path, sizeof(o->path), "%s", name->valuestring);
      o->sizes[0] = cJSON_IsTrue(backups);
      o->slot = cJSON_IsNumber(slot) ? slot->valueint : -1;
      e = ESP_OK;
      snprintf(
          message, sizeof(message), "Receber %s do Pi.%s", o->path,
          !strcmp(o->action, "pi-install")
              ? " Instalar somente app-only no slot solicitado apos validacao."
              : " Analise pelo SD antes de instalar.");
    }
  }
  if (e) {
    free(o);
    return reply(false, message[0] ? message : esp_err_to_name(e));
  }
  snprintf(o->ticket, sizeof(o->ticket), "%08lx%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random());
  pending = o;
  cJSON *r = reply(true, message);
  cJSON_AddStringToObject(r, "ticket", o->ticket);
  cJSON_AddBoolToObject(r, "willRestart", restarts(o));
  return r;
}

cJSON *web_ops_request(const cJSON *j) {
  if (!request_lock)
    request_lock = xSemaphoreCreateMutex();
  if (!request_lock || xSemaphoreTake(request_lock, 0) != pdTRUE)
    return reply(false, "Outro pedido em andamento");
  cJSON *out = request_locked(j);
  xSemaphoreGive(request_lock);
  return out;
}
static cJSON *remote_state(void) {
  cJSON *j = web_ops_status();
  cJSON_AddStringToObject(j, "launcher", esp_app_get_description()->version);
  cJSON_AddNumberToObject(j, "battery", board_battery_percent());
  cJSON_AddNumberToObject(j, "boot", boot_mgr_get_default());
  cJSON *slots = cJSON_AddArrayToObject(j, "slots");
  for (int i = 0; i < BOOT_MGR_MAX_SLOTS; i++) {
    const esp_partition_t *p = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0 + i, NULL);
    if (!p)
      continue;
    esp_app_desc_t d;
    bool valid = esp_ota_get_partition_description(p, &d) == ESP_OK;
    cJSON *s = cJSON_CreateObject();
    cJSON_AddNumberToObject(s, "i", i);
    cJSON_AddBoolToObject(s, "used", valid);
    cJSON_AddStringToObject(s, "name", valid ? d.project_name : "");
    cJSON_AddStringToObject(s, "project", valid ? d.project_name : "");
    cJSON_AddStringToObject(s, "version", valid ? d.version : "");
    cJSON_AddNumberToObject(s, "kb", p->size / 1024);
    cJSON_AddItemToArray(slots, s);
  }
  const cJSON *pct = cJSON_GetObjectItemCaseSensitive(j, "percent"),
              *msg = cJSON_GetObjectItemCaseSensitive(j, "message");
  cJSON_AddNumberToObject(j, "pct", pct ? pct->valueint : -1);
  cJSON_AddStringToObject(j, "job",
                          web_ops_busy() && msg ? msg->valuestring : "");
  return j;
}
static void remote_ask(const char *cmd, const char *arg, uint8_t **body,
                       size_t *length, int *code) {
  *code = 400;
  cJSON *j = NULL;
  if (!strcmp(cmd, "state")) {
    j = remote_state();
    *code = 200;
  } else if (!strcmp(cmd, "shot")) {
    if (!board_lvgl_lock(2000)) {
      *code = 503;
      return;
    }
    lv_draw_buf_t *image =
        lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    board_lvgl_unlock();
    if (!image || image->header.w != 1280 || image->header.h != 720) {
      *code = 503;
    } else {
      const char *header = "1280 720 rgb565\n";
      size_t h = strlen(header);
      *length = h + 1280 * 720 * 2;
      *body = malloc(*length);
      if (!*body) {
        *length = 0;
        *code = 503;
      } else {
        memcpy(*body, header, h);
        for (unsigned y = 0; y < 720; y++)
          for (unsigned x = 0; x < 1280; x++) {
            const uint8_t *p = image->data + y * image->header.stride + 4 * x;
            uint16_t color = (p[2] >> 3) << 11 | (p[1] >> 2) << 5 | (p[0] >> 3);
            size_t off = h + 2 * (y * 1280 + x);
            (*body)[off] = color;
            (*body)[off + 1] = color >> 8;
          }
        *code = 200;
      }
    }
    if (image && board_lvgl_lock(0)) {
      lv_draw_buf_destroy(image);
      board_lvgl_unlock();
    }
    return;
  } else if (!strcmp(cmd, "run") || !strcmp(cmd, "boot")) {
    char *end;
    long slot = strtol(arg, &end, 10);
    esp_err_t e = (*end || end == arg || slot < 0 ||
                   slot >= BOOT_MGR_MAX_SLOTS || web_ops_busy())
                      ? ESP_ERR_INVALID_ARG
                  : !strcmp(cmd, "boot")
                      ? boot_mgr_set_default(slot)
                      : boot_mgr_launch_async(slot, NULL, NULL);
    j = reply(!e, esp_err_to_name(e));
    *code = e ? 409 : 202;
  } else if (!strcmp(cmd, "backup") || !strcmp(cmd, "install")) {
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(
        req, "action", !strcmp(cmd, "backup") ? "pi-backup" : "pi-install");
    if (!strcmp(cmd, "install")) {
      char *end;
      long slot = strtol(arg, &end, 10);
      if (end == arg || *end != ' ') {
        cJSON_Delete(req);
        return;
      }
      while (*end == ' ')
        end++;
      if (end == arg || !sd_name_valid(end) || slot < 0 ||
          slot >= BOOT_MGR_MAX_SLOTS) {
        cJSON_Delete(req);
        return;
      }
      cJSON_AddStringToObject(req, "name", end);
      cJSON_AddNumberToObject(req, "slot", slot);
    }
    cJSON *plan = web_ops_request(req);
    cJSON_Delete(req);
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(plan, "ok"),
                *ticket = cJSON_GetObjectItemCaseSensitive(plan, "ticket");
    if (cJSON_IsTrue(ok) && cJSON_IsString(ticket)) {
      req = cJSON_CreateObject();
      cJSON_AddStringToObject(req, "action", "execute");
      cJSON_AddStringToObject(req, "ticket", ticket->valuestring);
      j = web_ops_request(req);
      cJSON_Delete(req);
      *code =
          cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "ok")) ? 202 : 409;
    } else {
      j = cJSON_Duplicate(plan, true);
      *code = 409;
    }
    cJSON_Delete(plan);
  } else {
    j = reply(false, "Comando desconhecido");
    *code = 404;
  }
  if (j) {
    *body = (uint8_t *)cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (*body)
      *length = strlen((char *)*body);
    else
      *code = 503;
  }
}
void web_ops_remote_init(void) {
  if (!request_lock)
    request_lock = xSemaphoreCreateMutex();
  if (!lock)
    lock = xSemaphoreCreateMutex();
  if (!operations)
    operations = xQueueCreate(1, sizeof(operation_t *));
  if (operations && !operation_task)
    xTaskCreate(operation_loop, "web_operation", 16384, NULL, 4,
                &operation_task);
  pi_link_set_ask_handler(remote_ask);
}
