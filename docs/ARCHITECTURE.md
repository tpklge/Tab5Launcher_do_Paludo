# Atualização 0.2.0 — seis etapas

Esta seção e SIX_STAGES.md prevalecem sobre previsões históricas abaixo.

- `sd_worker`: 48 KB de stack, arquivos/parser/instalador serializados, filas com geração.
- `settings`: 12 KB, persistência/hora/energia fora de callbacks LVGL; resultados por geração.
- `assets`: 24 KB, PNG/JPEG e blur fora da renderização; filas limitadas, tickets de
  objeto e descarte de respostas antigas; cache/dados de imagem liberados na exclusão.
- `apps_worker`: 24 KB, inventário, registro por identidade, padrão e remoção.
- Instalador: mutex global, digest integral do destino, backup verificado, fonte por
  região/hash e dados em destinos restritos. Profile 8 MB usa backup completo, NVS
  de retomada e espelho da tabela com registro de intenção consumido pelo bootloader.
- Guarda de boot usa NVS, sem confiar na RAM RTC de outro app. Main abre a contagem
  somente quando a política permite, sem aguardar Wi-Fi/NTP.
- RTC RX8130CE independente, codec UTC testável, backup charging desabilitado;
  monitor INA226/cache SD não executa I/O no ticker de status.
- LVGL usa FreeRTOS e malloc C/PSRAM; decodificação assíncrona não altera widgets.

---

# Atualização de arquitetura — SD / instalador / boot único

Esta seção substitui os detalhes históricos conflitantes abaixo.

- SDMMC slot **0** pertence ao BSP/cartão; SDIO slot **1** pertence ao C6.
- `sd_worker` (24 KB de stack, prioridade 3) serializa listagem, arquivos, análise,
  backup e instalação. UI apenas envia pedidos. Respostas/progresso usam filas
  limitadas com geração de tela; respostas antigas não acessam objetos destruídos.
- `firmware_parser.c` é independente do SDK; leitor/hash são callbacks limitados.
  Wrapper de arquivo usa blocos pequenos e SHA-256 mbedTLS.
- `installer_execute` possui mutex global e revalida origem/destino, backup,
  journal NVS, APIs OTA e verificação SDK. Somente app OTA inativo é gravado.
- `bootloader_components/launcher_policy` usa hooks oficiais e loader SDK;
  pedido CRC32 em `launch_req` é consumido antes de iniciar app uma vez.
  Boot sem pedido seleciona factory. Não depende de RTC de outro firmware.
- HOME inicia em worker de 8 KB. UI de arquivos usa Enter/Esc via fila A164.
- A arbitragem completa do SD com futuros MSC/WebUI não está implementada;
  esses serviços ainda não acessam o cartão.

---

# Tab5 Launcher — Architecture

## Component Dependency Graph

```
main
 ├── board          (display, SD, RTC, power, PI4IOE)
 ├── boot_mgr       (countdown, launch, guard) → board
 ├── storage        (NVS config, SD config.conf)
 ├── time_mgr       (RTC, NTP, timezone) → board, storage
 ├── input          (Tab5 Keyboard A164, I²C, ISR)
 ├── ui             (LVGL screens) → board, boot_mgr, storage, time_mgr, input, diagnostics
 ├── wifi           (ESP-Hosted, C6, states, recovery) → board
 ├── diagnostics    (reset reason, heap, log)
 ├── firmware       (parser: app-only / merged / multipart)  [Etapa 3]
 ├── installer      (InstallPlan, flash, verify) → firmware, partitions_mgr, storage  [Etapa 3]
 ├── partitions_mgr (layout, migration, inspection)  [Etapa 4]
 ├── catalog        (OTA providers, downloads)  [Etapa 5]
 ├── backup         (data, full-flash)  [Etapa 4]
 ├── web            (REST API, WebUI)  [Etapa 5]
 ├── usb            (host/device, MSC, console)  [Etapa 5]
 └── pi_link        (optional Raspberry Pi helper)  [Etapa 5]
```

## Bus Ownership

| Bus            | Owner          | Pins                          | Notes                                |
|----------------|----------------|-------------------------------|--------------------------------------|
| MIPI-DSI       | BSP/display    | (DSI lanes, internal)         | Exclusive; PPA rotates 1280×720→720×1280 |
| I²C port 0     | BSP (system)   | (BSP managed)                 | Touch, RTC, INA226, PI4IOE           |
| I²C port 0 kbd | input          | SDA=GPIO0, SCL=GPIO1          | Tab5 Keyboard A164 — own I²C master  |
| SDMMC slot 1   | board (SD card)| (BSP managed)                 | FAT32/exFAT; NOT shared with C6      |
| SDIO slot 0    | wifi (ESP-Hosted)| CLK=12, CMD=13, D0-3=11,10,9,8, RST=15 | ESP32-C6 Wi-Fi coprocessor |
| USB-A          | usb (Etapa 5)  | (hardware routing)            | Host for keyboard/mouse or MSC       |
| USB-C          | console/JTAG   | (hardware routing)            | Serial console and flashing          |

## FreeRTOS Tasks

| Task           | Stack  | Priority | Owner          | Notes                                  |
|----------------|--------|----------|----------------|----------------------------------------|
| LVGL port      | 8192   | 4        | esp_lvgl_port  | Runs all lv_* calls; acquire lock first |
| kbd_worker     | 3072   | 5        | input          | I²C polls + INT handler                |
| wifi_worker    | 6144   | 4        | wifi           | ESP-Hosted + esp_wifi                  |
| app_main       | 8192   | 1        | (deleted after boot) | Calls init sequence              |
| installer_task | 8192   | 3        | installer      | Etapa 3; streaming flash write         |
| catalog_task   | 6144   | 3        | catalog        | Etapa 5; HTTPS downloads               |
| web_task       | 8192   | 3        | web            | Etapa 5; HTTP server                   |

## LVGL Access Policy

- All `lv_*` calls MUST be made with `board_lvgl_lock()` held.
- Worker tasks use `ui_post(fn, arg)` to schedule UI updates.
- Callbacks registered with Wi-Fi, installer, etc. that touch the UI are called from the
  LVGL task (the caller acquires the lock before invoking the callback).
- ISRs (keyboard INT) only signal the worker task via `vTaskNotifyGiveFromISR`.

## Init / Deinit Order

**Init (app_main):**
1. `nvs_flash_init`
2. `diagnostics_early_init`
3. `board_init` → charger → display → SD → RTC
4. `storage_init` → NVS → SD config.conf
5. `time_init` → RTC → TZ → (NTP later)
6. `boot_mgr_guard_take`
7. `input_init` → keyboard worker
8. `ui_init` → LVGL port → screens
9. `wifi_init` → ESP-Hosted async
10. `boot_mgr_start` → countdown or stay

**Before launching another app (`board_shutdown`):**
1. `wifi_pause` — stop Wi-Fi, keep ESP-Hosted/SDIO (SD keeps working)
2. Backlight off
3. SD flush (implicit via bsp_sdcard_unmount or file close)
4. Display DMA — LVGL port deinit not needed if P4 restarts
5. Keyboard I²C bus released
6. `esp_restart()`

## Partition Layout (Default "2 Apps")

See `partitions.csv` and `docs/FLASH_LAYOUT.md`.
