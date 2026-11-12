#include "usb.h"
#include "storage_guard.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "usb_storage.h"
#endif
#include "backup.h"
#include "board.h"
#include "boot_mgr.h"
#include "cJSON.h"
#include "catalog.h"
#ifdef ESP_PLATFORM
#include "esp_system.h"
#include "esp_timer.h"
#endif
#ifdef ESP_PLATFORM
#include "flash_snapshot.h"
#include "web.h"
#endif
#include "diagnostics.h"
#include "esp_console.h"
#include "esp_random.h"
#include "installer.h"
#include "launcher_store.h"
#include "mbedtls/sha256.h"
#include "sd_files.h"
#include "wifi_mgr.h"
#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef USB_INCOMING_ROOT
#define USB_INCOMING_ROOT LAUNCHER_ROOT "/incoming"
#endif
static esp_console_repl_t *s_repl;
static esp_err_t (*s_view_handler)(const char *);
void usb_set_view_handler(esp_err_t (*handler)(const char *)) {
  s_view_handler = handler;
}
static FILE *s_upload;
static bool s_upload_lease;
static atomic_bool s_transferring, s_handoff_blocked;
void usb_set_handoff_blocked(bool value) {
  atomic_store(&s_handoff_blocked, value);
}
bool usb_transfer_active(void) { return atomic_load(&s_transferring); }
static char s_path[256], s_tmp[264], s_dir[128], s_hash[65];
static uint32_t s_expected, s_written;
static mbedtls_sha256_context s_sha;
static void reply(cJSON *o) {
  char *j = cJSON_PrintUnformatted(o);
  cJSON_Delete(o);
  if (j) {
    printf("\nTAB5 %s\n", j);
    free(j);
  }
}
static void result(esp_err_t err, const char *message) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "error", err);
  cJSON_AddStringToObject(o, "message",
                          message ? message : esp_err_to_name(err));
  reply(o);
}
static bool number(const char *text, uint32_t *n) {
  char *end;
  unsigned long v = strtoul(text, &end, 10);
  if (!*text || *end || *text == '-' || v > 32 * 1024 * 1024)
    return false;
  *n = v;
  return true;
}
static void abort_upload(void) {
  if (s_upload) {
    fclose(s_upload);
    s_upload = NULL;
    atomic_store(&s_transferring, false);
    mbedtls_sha256_free(&s_sha);
    unlink(s_tmp);
    rmdir(s_dir);
  }
  if (s_upload_lease) {
    s_upload_lease = false;
    storage_write_release();
  }
  s_expected = s_written = 0;
  atomic_store(&s_transferring, false);
}
static int tab5_impl(int argc, char **argv) {
  if (argc < 2) {
    result(ESP_ERR_INVALID_ARG,
           "status | screenshot | ls PATH PAGE | read PATH OFFSET LENGTH | "
           "begin NAME SIZE "
           "SHA256 | chunk OFFSET HEX | commit | abort | boot SLOT | backup "
           "SLOT | wifi-retry");
    return 0;
  }
  const char *cmd = argv[1];
  if (atomic_load(&s_handoff_blocked) && strcmp(cmd, "status") &&
      strcmp(cmd, "abort")) {
    result(ESP_ERR_INVALID_STATE, "Preparando troca de aplicativo");
    return 0;
  }
  if (!strcmp(cmd, "view") && argc == 3) {
    result(s_view_handler ? s_view_handler(argv[2]) : ESP_ERR_INVALID_STATE,
           "Navegacao solicitada");
  } else if (!strcmp(cmd, "status")) {
    diagnostics_snapshot_t d;
    diagnostics_get_snapshot(&d);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "error", 0);
    cJSON_AddStringToObject(o, "version", d.app_ver);
    cJSON_AddStringToObject(o, "reset", d.reset_desc);
    cJSON_AddNumberToObject(o, "internalFree", d.free_internal);
    cJSON_AddNumberToObject(o, "psramFree", d.free_psram);
    cJSON_AddBoolToObject(o, "sd", board_sd_ready());
    cJSON_AddBoolToObject(o, "upload", s_upload != NULL);
#ifdef ESP_PLATFORM
    cJSON_AddNumberToObject(o, "internalLargest",
                            heap_caps_get_largest_free_block(
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(
        o, "internalFree8Bit",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    usb_storage_status_t drive;
    usb_storage_status(&drive);
    cJSON_AddBoolToObject(o, "usbMounted", drive.mounted);
    cJSON_AddBoolToObject(o, "usbEjecting", drive.ejecting);
    cJSON_AddNumberToObject(o, "usbTotal", drive.total);
    cJSON_AddStringToObject(o, "usbError", drive.error);
    uint32_t disk_phase, disk_seconds;
    usb_disk_last_status(&disk_phase, &disk_seconds);
    cJSON_AddNumberToObject(o, "usbDiskPhase", disk_phase);
    cJSON_AddNumberToObject(o, "usbTransitionError",
                            usb_disk_transition_error());
    cJSON_AddNumberToObject(o, "usbDiskSeconds", disk_seconds);
    wifi_status_t wifi;
    wifi_get_status(&wifi);
    board_power_status_t power;
    board_power_status(&power);
    cJSON_AddNumberToObject(o, "uptimeUs", esp_timer_get_time());
    cJSON_AddNumberToObject(o, "internalMinimum", d.min_free_internal);
    cJSON_AddNumberToObject(o, "wifiState", wifi.state);
    cJSON_AddNumberToObject(o, "wifiReason", wifi.disconnect_reason);
    cJSON_AddNumberToObject(o, "rssi", wifi.rssi);
    cJSON_AddStringToObject(o, "c6Firmware", wifi.c6_fw);
    cJSON_AddBoolToObject(o, "powerValid", power.valid);
    cJSON_AddNumberToObject(o, "batteryMv", power.millivolts);
    cJSON_AddNumberToObject(o, "batteryMa", power.milliamps);
    cJSON_AddNumberToObject(o, "batteryEstimate", power.percent);
    cJSON_AddNumberToObject(o, "sdFree", power.sd_free);
#endif
    reply(o);
#ifdef ESP_PLATFORM
  } else if (!strcmp(cmd, "usb-eject")) {
    result(usb_storage_eject(), "Ejetando D:");
  } else if (!strcmp(cmd, "c6-update") && argc == 4 &&
             !strcmp(argv[3], "confirm")) {
    esp_err_t e = usb_update_c6(argv[2]);
    result(
        e,
        e ? "Falha ou estado indeterminado; reinicie e confira a versao do C6"
          : "C6 atualizado; reiniciando P4 para conferir a versao");
    if (!e && storage_prepare_reboot(5000)) {
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_restart();
    }
  } else if (!strcmp(cmd, "usb-disk") && argc == 3 &&
             !strcmp(argv[2], "confirm")) {
    esp_err_t e =
        (installer_is_busy() || installer_recovery_required() || s_upload)
            ? ESP_ERR_INVALID_STATE
            : usb_disk_request();
    result(e, e ? "Operacao ocupada"
                : "Solicitacao USB aceita; aguardando armazenamento ocioso. "
                  "Ejete no computador antes de sair.");
  } else if (!strcmp(cmd, "flash-backup")) {
    flash_snapshot_t snapshot;
    char message[600] = {0};
    esp_err_t e =
        installer_full_backup(&snapshot, NULL, NULL, message, sizeof(message));
    result(e, message);
  } else if (!strcmp(cmd, "flash-check") && argc == 3) {
    flash_snapshot_t snapshot;
    char message[600] = {0};
    esp_err_t e =
        flash_snapshot_validate(argv[2], &snapshot, message, sizeof(message));
    result(e, message);
  } else if (!strcmp(cmd, "flash-plan") && argc >= 3 && argc <= 6) {
    uint32_t sizes[4] = {0};
    bool valid = true;
    for (int i = 2; i < argc; i++) {
      uint32_t units = 0;
      if (!number(argv[i], &units) || units < 4 || units > 32) {
        valid = false;
        break;
      }
      sizes[i - 2] = units * 0x40000;
    }
    uint8_t hash[32];
    char message[600] = {0};
    esp_err_t e = valid ? flash_migrate_plan(sizes, argc - 2, hash, message,
                                             sizeof(message))
                        : ESP_ERR_INVALID_ARG;
    result(e, message);
  } else if (!strcmp(cmd, "web-start")) {
    esp_err_t e = web_start();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "error", e);
    if (!e) {
      char token[16];
      web_access_token(token, sizeof(token));
      wifi_status_t status;
      wifi_get_status(&status);
      cJSON_AddStringToObject(o, "ip", status.ip);
      cJSON_AddStringToObject(o, "code", token);
    }
    reply(o);
  } else if (!strcmp(cmd, "web-stop")) {
    web_stop();
    result(ESP_OK, NULL);
#endif
  } else if (!strcmp(cmd, "ls") && argc == 4) {
    uint32_t page;
    if (!sd_path_valid(argv[2]) || !number(argv[3], &page)) {
      result(ESP_ERR_INVALID_ARG, NULL);
      return 0;
    }
    sd_listing_t *l = malloc(sizeof(*l));
    if (!l) {
      result(ESP_ERR_NO_MEM, NULL);
      return 0;
    }
    int e = sd_files_list(argv[2], page, l);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "error", e);
    cJSON *a = cJSON_AddArrayToObject(o, "entries");
    if (!e)
      for (unsigned i = 0; i < l->count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "name", l->entries[i].name);
        cJSON_AddNumberToObject(f, "size", l->entries[i].size);
        cJSON_AddBoolToObject(f, "directory", l->entries[i].directory);
        cJSON_AddItemToArray(a, f);
      }
    free(l);
    reply(o);
  } else if (!strcmp(cmd, "read") && argc == 5) {
    uint32_t offset, len;
    if (!sd_path_valid(argv[2]) || !number(argv[3], &offset) ||
        !number(argv[4], &len) || len > 256) {
      result(ESP_ERR_INVALID_ARG, NULL);
      return 0;
    }
    if (!sd_media_acquire(argv[2])) {
      result(ESP_ERR_INVALID_STATE, NULL);
      return 0;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) {
      sd_media_release(argv[2]);
      result(ESP_ERR_NOT_FOUND, NULL);
      return 0;
    }
    uint8_t bytes[256];
    char hex[513];
    size_t n = 0;
    bool ok = fseek(f, offset, SEEK_SET) == 0;
    if (ok)
      n = fread(bytes, 1, len, f);
    ok = ok && !ferror(f);
    fclose(f);
    sd_media_release(argv[2]);
    for (unsigned i = 0; i < n; i++)
      snprintf(hex + 2 * i, 3, "%02x", bytes[i]);
    hex[2 * n] = 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "error", ok ? 0 : ESP_FAIL);
    cJSON_AddNumberToObject(o, "offset", offset);
    cJSON_AddStringToObject(o, "hex", hex);
    reply(o);
  } else if (!strcmp(cmd, "begin") && argc == 5) {
    uint32_t size;
    bool hash = strlen(argv[4]) == 64;
    for (const char *p = argv[4]; *p; p++)
      if (!isxdigit((unsigned char)*p))
        hash = false;
    if (s_upload || installer_is_busy() || !board_sd_ready() ||
        !sd_name_valid(argv[2]) || strlen(argv[2]) > 95 ||
        !number(argv[3], &size) || !size || size > 16 * 1024 * 1024 || !hash) {
      result(ESP_ERR_INVALID_ARG,
             "Transferencia ativa ou parametros invalidos");
      return 0;
    }
    s_upload_lease = storage_write_acquire();
    if (!s_upload_lease) {
      abort_upload();
      result(ESP_ERR_INVALID_STATE, "Armazenamento reservado");
      return 0;
    }
    mkdir(USB_INCOMING_ROOT, 0755);
    snprintf(s_dir, sizeof(s_dir), USB_INCOMING_ROOT "/usb-%08lx-%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    if (mkdir(s_dir, 0755)) {
      abort_upload();
      result(ESP_FAIL, NULL);
      return 0;
    }
    snprintf(s_path, sizeof(s_path), "%s/%s", s_dir, argv[2]);
    snprintf(s_tmp, sizeof(s_tmp), "%s.part", s_path);
    s_upload = fopen(s_tmp, "wx");
    if (!s_upload) {
      rmdir(s_dir);
      abort_upload();
      result(ESP_FAIL, NULL);
      return 0;
    }
    atomic_store(&s_transferring, true);
    s_expected = size;
    s_written = 0;
    strcpy(s_hash, argv[4]);
    mbedtls_sha256_init(&s_sha);
    mbedtls_sha256_starts(&s_sha, 0);
    result(ESP_OK, "Pronto para chunks de ate 128 bytes");
  } else if (!strcmp(cmd, "chunk") && argc == 4) {
    uint32_t offset;
    size_t chars = strlen(argv[3]);
    uint8_t bytes[128];
    bool ok = s_upload && number(argv[2], &offset) && offset == s_written &&
              chars > 0 && chars <= 256 && chars % 2 == 0 &&
              chars / 2 <= s_expected - s_written;
    for (size_t i = 0; ok && i < chars; i++)
      if (!isxdigit((unsigned char)argv[3][i]))
        ok = false;
    if (!ok) {
      result(ESP_ERR_INVALID_ARG, "Offset ou hexadecimal invalido");
      return 0;
    }
    for (size_t i = 0; i < chars / 2; i++) {
      char pair[3] = {argv[3][i * 2], argv[3][i * 2 + 1], 0};
      bytes[i] = strtoul(pair, NULL, 16);
    }
    size_t n = chars / 2;
    if (fwrite(bytes, 1, n, s_upload) != n) {
      abort_upload();
      result(ESP_FAIL, "Falha ao gravar; temporario removido");
      return 0;
    }
    mbedtls_sha256_update(&s_sha, bytes, n);
    s_written += n;
    result(ESP_OK, "Chunk recebido");
  } else if (!strcmp(cmd, "commit")) {
    if (!s_upload || s_written != s_expected) {
      result(ESP_ERR_INVALID_STATE, "Transferencia incompleta");
      return 0;
    }
    uint8_t digest[32];
    char hex[65];
    mbedtls_sha256_finish(&s_sha, digest);
    for (unsigned i = 0; i < 32; i++)
      snprintf(hex + i * 2, 3, "%02x", digest[i]);
    if (strcasecmp(hex, s_hash)) {
      abort_upload();
      result(ESP_ERR_INVALID_CRC, "SHA-256 divergiu; temporario removido");
      return 0;
    }
    bool ok = fflush(s_upload) == 0 && fsync(fileno(s_upload)) == 0;
    if (fclose(s_upload))
      ok = false;
    s_upload = NULL;
    atomic_store(&s_transferring, false);
    mbedtls_sha256_free(&s_sha);
    if (ok && rename(s_tmp, s_path) == 0)
      result(ESP_OK, s_path);
    else {
      unlink(s_tmp);
      rmdir(s_dir);
      result(ESP_FAIL, "Publicacao falhou");
    }
    if (s_upload_lease) {
      s_upload_lease = false;
      storage_write_release();
    }
  } else if (!strcmp(cmd, "abort")) {
    abort_upload();
    result(ESP_OK, "Transferencia cancelada");
  } else if (!strcmp(cmd, "boot") && argc == 3) {
    uint32_t slot;
    if (!number(argv[2], &slot) || slot >= BOOT_MGR_MAX_SLOTS || s_upload ||
        installer_is_busy())
      result(ESP_ERR_INVALID_ARG, NULL);
    else {
      result(ESP_OK, "Iniciando slot verificado");
      result(boot_mgr_launch_slot(slot), NULL);
    }
  } else if (!strcmp(cmd, "backup") && argc == 3) {
    uint32_t slot;
    char message[512] = "";
    esp_err_t e = number(argv[2], &slot)
                      ? backup_slot(slot, NULL, NULL, message, sizeof(message))
                      : ESP_ERR_INVALID_ARG;
    result(e, message);
  } else if (!strcmp(cmd, "wifi-retry")) {
    wifi_retry();
    result(ESP_OK, "Nova tentativa solicitada");
  } else if (!strcmp(cmd, "ota-check")) {
    char url[512], message[512] = "";
    if (argc == 3)
      snprintf(url, sizeof(url), "%s", argv[2]);
    else
      catalog_source_get(url, sizeof(url));
    catalog_list_t *list = calloc(1, sizeof(*list));
    if (!list)
      result(ESP_ERR_NO_MEM, NULL);
    else {
      esp_err_t e = catalog_fetch(url, list, message, sizeof(message));
      if (!e)
        snprintf(message, sizeof(message), "Catalogo valido: %u firmware(s)",
                 list->count);
      result(e, message);
      free(list);
    }
  } else if (!strcmp(cmd, "asset") && argc == 4) {
    esp_err_t e = launcher_web_publish(argv[2], argv[3]);
    result(e, NULL);
  } else if (!strcmp(cmd, "screenshot")) {
    char path[128];
    snprintf(path, sizeof(path), LAUNCHER_DIAGNOSTICS "/screenshot-%08lx.bmp",
             (unsigned long)esp_random());
    esp_err_t e = diagnostics_capture(path);
    result(e, e == ESP_OK ? path : NULL);
  } else
    result(ESP_ERR_INVALID_ARG, "Comando ou argumentos invalidos");
  return 0;
}
static int tab5(int argc, char **argv) {
  const char *cmd = argc > 1 ? argv[1] : "";
  if (!strcmp(cmd, "usb-disk") || !strcmp(cmd, "boot") ||
      !strcmp(cmd, "status"))
    return tab5_impl(argc, argv);
  storage_io_scope_t scope
      __attribute__((cleanup(storage_io_scope_leave))) = {storage_io_acquire()};
  if (!scope.held) {
    result(ESP_ERR_INVALID_STATE, "Armazenamento reservado para troca de modo");
    return 0;
  }
  return tab5_impl(argc, argv);
}
esp_err_t usb_init(void) {
  if (s_repl)
    return ESP_OK;
  esp_console_repl_config_t c = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  c.task_stack_size = 16384;
  c.max_history_len = 1;
  c.prompt = "tab5>";
  c.max_cmdline_length = 768;
  esp_console_dev_usb_serial_jtag_config_t device =
      ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  esp_err_t e = esp_console_new_repl_usb_serial_jtag(&device, &c, &s_repl);
  if (e)
    return e;
  esp_console_cmd_t cmd = {
      .command = "tab5",
      .help = "Recuperacao e arquivos SD (tab5 sem argumentos mostra comandos)",
      .func = tab5};
  e = esp_console_cmd_register(&cmd);
  if (!e)
    e = esp_console_start_repl(s_repl);
  return e;
}
