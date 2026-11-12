Estado atual: **0.6.0**. A seção final atualiza as linhas históricas abaixo; consulte também [limites atuais](KNOWN_LIMITATIONS.md).

# Revisão 0.3.1 — configuração no SD e WebUI responsiva

Token de 8 dígitos, fontes maiores, CFG em três colunas, paleta de cores e botão
de desligar. HTML/CSS/JS no microSD; preferências em `/launcher`; adaptadores OTA
para M5Burner oficial, LauncherHub e manifesto Loadout. Detalhes e validação em
[CHANGES_0.3.1.md](CHANGES_0.3.1.md). Este estado prevalece sobre o histórico abaixo.

---

# Revisão 0.3.0 — serviços e manutenção

Catálogo HTTPS próprio e download multipartes com SHA-256, WebUI autenticada para
status/SD/upload, console USB com transferência verificada, inspeção/backup de slots,
teste de teclado/recuperação Wi-Fi, exportação de logs/captura BMP, configuração/temas
JSON estritos e integração Pi opcional. Contratos, uso e **itens ainda pendentes**
em [SERVICES_0.3.0.md](SERVICES_0.3.0.md). Build/testes host e **flash 0.3.0 concluídos**. Serial confirmou boot, dois slots,
SD e reconexão Wi-Fi; os novos fluxos de interface/Pi ainda precisam de teste físico.
Este estado prevalece sobre as matrizes históricas abaixo.

---

# Feature Parity Matrix

Audit date: 2026-11-11.
Sources: UNIZERSION/tab5-launcher @ da5aa984, bmorcelli/Launcher @ 2f43aa7b.

**A revisão 0.2.0 e a matriz abaixo prevalecem sobre os estados históricos.**
Os seis grupos estão detalhados em [SIX_STAGES.md](SIX_STAGES.md); validação física
continua separada.

States: **[HOST]** implemented and tested in host, physical validation tracked in PROGRESS | **[DONE]** implemented in this project | **[WIP]** in progress this etapa |
        **[TODO-N]** planned for Etapa N | **[N/A]** not applicable to Tab5 | **[RESEARCH]** needs investigation.

---

## Seis grupos — 0.2.0

| Função | Implementação | Verificação | Estado |
|---|---|---|---|
| Espaço livre FATFS, unknown distinto de zero | board.c / ui_status_bar.c | test_status; build | [HOST/BUILD] |
| Ordenação global, pastas recursivas, editor com versão anterior | sd_files.c / ui_sd.c | tests/sd_files + test_sd | [HOST] |
| Restaurar backups app/dados com manifesto/hash | firmware.c / installer.c / ui_sd.c | tests/installer + bundles | [HOST] |
| Merged de base 0 e pacote próprio multipartes app/dados | firmware.c / installer.c | bundles / installer | [HOST] |
| Perfil 8 MB, backup completo, espelho e retomada | partitions_mgr / hooks / installer | layout + transaction + boot_policy | [HOST] |
| Nome, ícone assíncrono, padrão de boot, reativação | app_registry / ui_home / assets | registry + test_home + assets | [HOST] |
| Autoboot, botão manual, retorno/deep sleep, falhas persistentes | boot_mgr / main / ui_boot | boot_runtime + boot_policy | [HOST/BUILD] |
| RTC UTC, ajuste offline/fuso, carga/power-off | board_rtc / codec / ui_cfg | rtc + test_cfg; build hardware | [HOST/BUILD] |
| Temas, destaque, wallpaper, relógios e despertar | ui_theme / assets / visual / cfg | test_visual + assets + status + cfg | [HOST] |

## Matriz histórica da auditoria

## Boot & Startup

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Boot countdown ring | UNIZERSION | ui/bootscreen | Circular arc, configurable seconds | Yes | ui/ui_boot.c | ui_boot_start() | [DONE] |
| LAUNCHER / START NOW buttons | UNIZERSION | ui/bootscreen | Two buttons on boot screen | Yes | ui/ui_boot.c | — | [DONE] |
| Boot default app | UNIZERSION | l5_slots_handoff | Select ota slot + restart | Yes | boot_mgr | boot_mgr_launch_slot() | [DONE] |
| Return to launcher (RESET) | UNIZERSION / independent policy | launcher_policy/hooks.c | Durable one-shot request, default factory | Yes | boot_mgr / bootloader | tests/boot_policy | [HOST] |
| Boot-loop guard | UNIZERSION | l5_slots_guard_take | Counter policy not complete; one-shot prevents repeated automatic app boot | Yes | boot_mgr | pending physical cross-app tests | [WIP] |
| btnOnlyStart mode | UNIZERSION | l5_config.h | Skip bootscreen if screen not touched | Yes | boot_mgr / storage | — | [TODO-2] |
| Deep sleep start | UNIZERSION | l5_config.h | Wake from deep sleep → launcher | Yes | boot_mgr | — | [TODO-2] |

## HOME & Navigation

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Side menu | UNIZERSION | ui/l5_rail.c | Fixed left panel with tabs | Yes | ui/ui.c | — | [DONE] |
| App carousel | UNIZERSION | ui/l5_home.c | Horizontal swipe, center card highlighted | Yes | ui/ui_home.c | — | [DONE] |
| Navigation dots | UNIZERSION | ui/l5_home.c | Dots below carousel | Yes | ui/ui_home.c | pending | [TODO-6] |
| App cards with icon | UNIZERSION | ui/l5_home.c | Icon, name, version, date, slot info | Yes | ui/ui_home.c | partial | [WIP] |
| Empty slot install shortcut | UNIZERSION | ui/l5_home.c | "+" in empty slot opens installer | Yes | ui/ui_home.c | — | [TODO-3] |
| Start / set default / rename | UNIZERSION | l5_slots.h | Per-app actions in home card | Yes | ui / boot_mgr | — | [TODO-3] |
| App icon from SD (async) | UNIZERSION | l5_appicon.c | Reads apps/<proj>/icon.png | Yes | ui | — | [TODO-2] |
| Status bar | Both | l5_board.h | Time, battery, Wi-Fi, SD, keyboard | Yes | ui/ui_status_bar.c | — | [DONE] |

## Themes & Appearance

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Graphite theme | UNIZERSION | ui/l5_theme.c | Dark slate | Yes | ui/ui_theme.c | — | [DONE] |
| Midnight theme | UNIZERSION | ui/l5_theme.c | Deep blue-black | Yes | ui/ui_theme.c | — | [DONE] |
| Daylight theme | UNIZERSION | ui/l5_theme.c | Light/white | Yes | ui/ui_theme.c | — | [DONE] |
| Neon theme | UNIZERSION | ui/l5_theme.c | Black, glowing accent borders | Yes | ui/ui_theme.c | — | [DONE] |
| Live theme switch | UNIZERSION | ui/l5_theme.c | No reboot needed | Yes | ui | — | [TODO-2] |
| Accent color | UNIZERSION | l5_config.h | Fixed, from icon, from wallpaper | Yes | ui | — | [TODO-2] |
| Wallpaper from SD | UNIZERSION | ui/l5_wallpaper.c | JPEG/PNG, thumbnails, picker | Yes | ui | — | [TODO-2] |
| Idle clock (4 styles) | UNIZERSION | ui/l5_idle.c | Digital/Big/Thin/Analog over blur/clear/black | Yes | ui | — | [TODO-2] |
| Screen dim/off | UNIZERSION | l5_config.h | Backlight dim, screen off, brightness | Yes | ui / board | — | [TODO-2] |
| Importable themes (data files) | Prompt only | — | JSON theme schema, no code execution | Yes | ui | — | [TODO-6] |

## Wi-Fi

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| ESP-Hosted + ESP32-C6 | UNIZERSION | l5_wifi.c | SDIO transport, esp_wifi_remote | Yes | wifi/wifi_mgr.c | — | [WIP] |
| 11-state machine | Prompt | — | DISABLED → INIT_C6 → … → CONNECTED | Yes | wifi/wifi_mgr.c | — | [DONE] |
| Autoconnect at boot | UNIZERSION | l5_wifi_init | Join most recently successful saved network when enabled | Yes | wifi/wifi_mgr.c | — | [DONE] |
| Scan + connect + forget | UNIZERSION | l5_wifi.h | Full SSID management | Yes | wifi/wifi_mgr.c | partial | [WIP] |
| 6-level recovery | Prompt | — | retry/reconnect/restart-driver/reset-C6/forget/diag | Yes | wifi/wifi_mgr.c | — | [WIP] |
| SSID/pass verbatim (no trim) | Prompt | — | Preserve spaces, symbols, bytes | Yes | wifi/wifi_mgr.c | — | [DONE] |
| C6 version check | UNIZERSION | l5_wifi.h | Mismatch → FAILED + mismatch flag | Yes | wifi | — | [TODO-2] |
| AP+STA mode | UNIZERSION | l5_wifi.h | Only when stack supports | Yes | wifi | — | [RESEARCH] |
| QR code for AP | bmorcelli | web/WUI | Show QR code with AP URL | Yes | web / ui | — | [TODO-5] |

## Keyboard

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| A164 I²C HID driver | Both | l5_kbd.c / interface.cpp | INT + polling, HID mode, event queue | Yes | input/input.c | — | [WIP] |
| Hot-plug detection | Both | l5_kbd.c | Worker scans every 2 s | Yes | input/input.c | — | [DONE] |
| Arrow keys, Enter, Esc, Tab… | UNIZERSION | l5_kbd.h | Full key_kind_t set | Yes | input/input.c | — | [DONE] |
| Keyboard focus in all screens | UNIZERSION | l5_keys.c | LVGL sends keys to focused widget | Yes | ui | — | [TODO-2] |
| Modifier key state cleared on disconnect | Prompt | — | No stuck Ctrl/Shift | Yes | input/input.c | — | [TODO-2] |
| Keyboard reinit / bus recovery | Prompt | — | Exposed in Settings > Input | Yes | input | — | [DONE] |
| Keyboard test screen | Prompt | — | Show raw key presses | Yes | ui | — | [TODO-2] |
| Virtual on-screen keyboard | bmorcelli | — | LVGL built-in keyboard when no A164 | Yes | ui | — | [TODO-2] |

## SD & File Manager

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Browse, copy, rename, delete | Both | l5_files.c / SD.cpp | Full file manager | Yes | storage / ui | — | [TODO-2] |
| Install .bin from SD | Both | l5_inst.c | App-only + merged | Yes | installer | — | [TODO-3] |
| Large file (≥3.3 MB) no panic | Prompt | — | Streaming, limited buffer | Yes | installer / storage | — | [TODO-3] |
| FAT32 + exFAT | bmorcelli | — | Per BSP/driver support | Yes | board | — | [TODO-2] |
| Never auto-format on mount fail | Prompt | — | Mount fail → error message | Yes | board/board.c | — | [DONE] |

## Installer

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| App-only .bin | Both | l5_inst.c | Install without manual partitioning | Yes | installer | — | [TODO-3] |
| Merged .bin | Both | l5_inst.c | Extract app from merged image | Yes | installer | — | [TODO-3] |
| Multipart package | bmorcelli | — | bootloader+table+app+data+manifest | Yes | installer | — | [TODO-3] |
| InstallPlan before write | Prompt | — | Show what will be erased/written | Yes | installer | — | [TODO-3] |
| Streaming write (no full RAM buffer) | Prompt | — | Buffer-limited, progress in bytes | Yes | installer | — | [TODO-3] |
| Chip/revision validation | UNIZERSION | l5_fw.c | Reject wrong chip ID | Yes | firmware | — | [TODO-3] |
| aurora_cfg preservation | UNIZERSION | l5_ptable.c | Never overwrite 0xF10000 | Yes | partitions_mgr | — | [TODO-4] |

## OTA & Catalog

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| M5Stack Tab5 firmware catalog | Both | l5_net.c | Filter by Tab5/ESP32-P4 | Yes | catalog | — | [TODO-5] |
| GitHub Releases | bmorcelli | OTA.cpp | Custom URL support | Yes | catalog | — | [TODO-5] |
| Favorites | Both | l5_config.h (l5_favs_t) | Persistent favorites list | Yes | catalog | — | [TODO-5] |
| Download to SD then install | Both | — | Preserve all multipart components | Yes | catalog + installer | — | [TODO-5] |
| HTTPS with cert validation | Prompt | — | No global TLS disable | Yes | catalog / web | — | [TODO-5] |
| Resume download (Range) | Prompt | — | Resume on reconnect | Yes | catalog | — | [TODO-5] |

## WebUI

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| File manager (upload, download, edit) | bmorcelli | webUi/ | Equivalent to SD tab | Yes | web | — | [TODO-5] |
| Install firmware via WebUI | bmorcelli | webUi/ | Same policy as screen installer | Yes | web + installer | — | [TODO-5] |
| NVS editor | bmorcelli | webUi/ | Typed keys, backup before destructive | Yes | web | — | [TODO-5] |
| Authentication | Prompt | — | Random initial cred, session expiry | Yes | web | — | [TODO-5] |
| CSRF protection | Prompt | — | Against cross-origin requests | Yes | web | — | [TODO-5] |

## USB

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Screenshot via USB/serial | UNIZERSION | tools/tab5_shot.py | Python tool, PNG output | Yes | usb / tools | — | [TODO-5] |
| SD file transfer via USB/serial | UNIZERSION | tools/sd_put.py | Send/receive files | Yes | usb / tools | — | [TODO-5] |
| MSC (SD as USB drive) | bmorcelli | — | Deinit SD first, block concurrent access | Yes | usb | — | [TODO-5] |
| USB HID host (keyboard/mouse) | Prompt | — | Additional to built-in keyboard | Yes | usb | — | [TODO-5] |

## Raspberry Pi Integration

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Pi detection + handshake | UNIZERSION | l5_pi.c | USB handshake, version/caps | Yes | pi_link | — | [TODO-5] |
| App shelf on Pi | UNIZERSION | l5_pi.c | List/send/download/install | Yes | pi_link | — | [TODO-5] |
| Flash backups on Pi | UNIZERSION | l5_pi.c | Auto backup before destructive ops | Yes | pi_link | — | [TODO-5] |
| Pi web page | UNIZERSION | l5_pi.c | tab5link 2.0.0 protocol | Yes | pi_link | — | [TODO-5] |
| NTP from Pi | UNIZERSION | l5_pi.c | Sync time when Pi clock valid | Yes | pi_link / time_mgr | — | [TODO-5] |

## Time & RTC

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| RTC RX8130CE init | UNIZERSION | l5_board_sensors_init | BSP RTC init, detect invalid | Yes | board / time_mgr | — | [DONE] |
| Timezone (America/Cuiaba default) | Prompt | — | POSIX TZ, UTC-4, no DST | Yes | time_mgr / storage | — | [DONE] |
| Manual time entry | Prompt | — | UI form: day/month/year/hour/min/sec | Yes | time_mgr / ui | — | [TODO-2] |
| NTP async | Prompt | — | Does not block boot | Yes | time_mgr | — | [DONE] |
| Boot offline without NTP wait | Prompt | — | NTP is optional | Yes | time_mgr | — | [DONE] |

## Diagnostics

| Feature | Source | Symbol/File | Behavior | Applicable | Module | Test | State |
|---------|--------|-------------|----------|------------|--------|------|-------|
| Reset reason + heap at boot | Both | — | Logged to serial | Yes | diagnostics | — | [DONE] |
| Circular log | Prompt | — | 64-line ring, exportable | Yes | diagnostics | — | [DONE] |
| Diagnostics screen in UI | Prompt | — | Heap, SD, Wi-Fi, RTC, keyboard | Yes | ui / diagnostics | — | [TODO-2] |
| Coredump | UNIZERSION | sdkconfig | 64 KB coredump partition | Yes | diagnostics | — | [TODO-6] |

## Requirements Unique to This Project (not in references)

| Feature | Source | Behavior | Module | State |
|---------|--------|----------|--------|-------|
| 11-state Wi-Fi machine | Prompt | Full recovery hierarchy | wifi | [WIP] |
| InstallPlan object | Prompt | Show impact before writing | installer | [TODO-3] |
| Streaming install (no full-RAM buffer) | Prompt | Buffer-limited flash write | installer | [TODO-3] |
| Importable JSON themes | Prompt | Schema-validated, no code | ui | [TODO-6] |
| NVS config schema versioning | Prompt | Migration on upgrade | storage | [DONE] |
| Verbatim SSID/pass storage | Prompt | No trim, no case change | wifi | [DONE] |
| Independent SD from Wi-Fi | Prompt | Separate SDMMC/SDIO buses | board | [DONE] |
| Português do Brasil default | Prompt | UI language (Etapa 6) | ui | [TODO-6] |

## SD / instalação — revisão implementada nesta sessão

| Função | Código | Teste | Estado |
|---|---|---|---|
| SD com painel de detalhes e páginas de 24 entradas | ui_sd.c | tests/host_ui/test_sd.c | [HOST] |
| Criar pasta, copiar/cancelar, rename, delete não recursivo, prévia 4 KB | sd_files.c | tests/sd_files/run.py | [HOST] |
| Parser app-only P4 0x12, revisão/segmentos/checksum/SHA | firmware_parser.c | tests/firmware/run.py | [HOST] |
| Plano para slot existente, app/dados identificados e consentimento app-only | installer.c / ui_sd.c | tests/installer, tests/host_ui | [HOST] |
| Backup integral do slot com manifesto/hash antes de substituir | installer.c | tests/installer | [HOST] |
| Escrita OTA em blocos + verificação SDK + journal | installer.c | tests/installer | [HOST] |
| Origem/destino alterados, cancelamento e falhas de backup/journal/SDK | installer.c | tests/installer | [HOST] |
| Boot único e retorno na inicialização seguinte | launcher_policy/hooks.c | tests/boot_policy | [HOST] |
| Merged/multipartes, migração/layout automático e dados por app | — | pendente | [TODO-4] |

As linhas históricas [DONE] acima não comprovam testes físicos. PROGRESS.md e
TEST_PLAN.md registram as evidências atuais e prevalecem sobre previsões antigas.

Validação física da revisão: usuário confirmou SD, instalação de microcity 0.11.0 e retorno ao launcher por RESET; serial identificou a execução. Ver PROGRESS.md.

## Três etapas — progresso/apps, teclado e energia

| Função | Código | Teste | Estado |
|---|---|---|---|
| Barra, percentual e etapa da instalação/remoção | ui_progress.c / ui_sd.c | test_nav / test_sd | [HOST] |
| Dois slots 4 MB, preservando dados na migração específica | partitions.csv / prepare_multiboot_upgrade.py | tests/multiboot | [HOST] |
| HOME lista todos os slots, inclusive lacunas e livres | ui_home.c | test_home | [HOST] |
| Desinstalar com confirmação, digest e backup antes de apagar | installer.c / ui_home.c | tests/installer / test_home | [HOST] |
| Teclado LVGL com press/release, Enter, Tab/setas e foco modal | ui_nav.c / ui.c | test_nav / test_sd / test_wifi | [HOST] |
| Tensão/corrente INA226 e estimativa de bateria 2S | board_power.c / power_model.c | tests/power | [HOST] |
| Status de energia, Wi-Fi, SD livre e teclado | ui_status_bar.c / ui_diag.c | build; teste físico pendente | [BUILD] |

### Atualização 0.4.0 — 11/11/2026

A revisão atual de transferência, cache e interface está descrita em
[CHANGES_0.4.0.md](CHANGES_0.4.0.md). Ela substitui estados anteriores destes itens:

| Função | Estado atual | Evidência |
|---|---|---|
| Pastas na WebGUI, overlay, falha/reenvio | Implementado | Navegador real 4 larguras; testes físicos de HTTP separados |
| Cookie de sessão e atualização da lista | Implementado | HttpOnly/SameSite, expiração/revogação e testes de navegador |
| Cache de catálogo no SD | Implementado | Cache com hash/fonte; corrupção e atualização forçada no host |
| Detalhes OTA e baixar/instalar | Implementado | Metadados opcionais, consulta limitada de capa e tamanho; UI host |
| Fontes para português | Implementado | Montserrat Latin Extended e combinantes; glifos LVGL verificados |
| Seletores de imagem e data/hora brasileira | Implementado | Worker de diretório, filtros PNG/JPEG; calendário/máscaras no host |
| Editor visual de partições | Implementado com escopo delimitado | 1–4 slots no banco de apps; backup integral; testes de transação/falha |
| Backup extra ao desinstalar | Implementado com limite de identificação | Associação de pacote ou partição possivelmente compartilhada |
| Migração preservando apps ao reparticionar | Pendente | Editor atual remove apps após backup e pede reinstalação |
| Posse de NVS/SPIFFS de binários isolados | Não inferível automaticamente | Autor/pacote precisa fornecer associação |

0.4.1/0.4.2: lançamento SD fora da fila de arquivos, pausa Wi-Fi confirmada pelo worker e fallback de montagem SPI após firmware externo. Ver CHANGES_0.4.2.md.

### 0.5.0 — itens 1 a 3

| Recurso | Estado | Evidência |
|---|---|---|
| Redimensionar slots preservando apps | Implementado no banco de 8 MiB | Atribuição/limites, cópia de backup verificado, falha e retomada em host |
| Backup/restauração integral | Implementado para imagem compatível na tela; cross-version por USB | Preflight de bootloader/launcher, SHA, marcador persistente; testes host |
| Perfil FAT/SPIFFS/NVS por app | Implementado com associação explícita/pacote | Snapshots imutáveis, troca/retorno, rollback e NVS por namespace |
| Retomada OTA | Implementado conforme suporte da origem | Range, ETag, 200/206, integridade e arquivos parciais no host |
| Consulta de atualizações em lote | Implementado | Origens por identidade, consultas agrupadas e candidatos por versão |

Contratos e uso: [CHANGES_0.5.0.md](CHANGES_0.5.0.md). Migração arbitrária de partições
de dados, retenção automática dos snapshots e teste físico com corte de energia
continuam fora da validação desta revisão.

### 0.6.0 — itens 4 a 7

WebGUI compacta sem paginação, gerenciamento/flash em worker; exclusão recursiva;
MSC exclusivo; fontes SD e preparador de imagens; prateleira/backup/ASK Pi;
calibração parametrizada, coleta de ensaios e atualizador C6.
Ver CHANGES_0.6.0.md para contratos e BUILD_0.6.0.json para testes reais.
Pi, carga/autonomia, hot-plug e medições externas não são validados por simulação.

## Revisão 0.7.0 — requisitos desta sessão

| Função | Implementação | Evidência |
|---|---|---|
| Escala de cores 12×12 | ui_cfg.c | test_cfg: 144 cores únicas, 12 colunas e teclado |
| Launcher mínimo e apps por tamanho | layout_model / flash_snapshot / installer | Host: migração, falha/retomada e guardas. Tab5: dois apps, tabela alterada, primeiro app preservado e Microcity iniciado |
| Backup opcional na remoção | installer_uninstall_options / HOME / WebGUI | Host e duas remoções reais com backup desmarcado |
| Dados/ícones sob /launcher | launcher_paths / app_registry / storage | Migração das pastas existentes observada; arquivos novos e ícones geridos no diretório |
| Upload substitui arquivo e mescla pastas | web.c | HTTP real em C: e D:, SHA/releitura e arquivos irmãos preservados |
| USB-A como D: | usb_host_bus / usb_storage / sd_files | Pendrive real de 61,5 GB, leitura/escrita, ejeção e persistência após remontar |
| Fontes OTA no cartão e detalhes laterais | catalog_sources / ui_services | JSON manual, agregação/cache/cancelamento/falha parcial e ciclo de vida LVGL em host |
| Avisos/registro de energia | energy / board_power | CSV real; políticas configuráveis. Autonomia, corte crítico e calibração física não ensaiados |

Detalhes e limites em CHANGES_0.7.0.md; evidências em BUILD_0.7.0.json.
Editor gráfico manual implementado no Tab5 e WebGUI, junto à alocação automática. Aplicação física de reservas de 1/2 MiB e 2/1 MiB, seguida de compactação; limites de apps ocupados e movimentação de conteúdo também verificados em host. Navegação real por 25 telas após a redução de RAM não apresentou reinício.


## Revisão 0.7.1

Nove temas disponíveis no Tab5/JSON/WebGUI, incluindo Ocean, Forest, Sunset, Lavender e Sand. Descanso mostra hora ampliada e data por extenso em português com acentos; formatação independente de locale. MSC usa buffer de 2 KiB, enumeração antecipada, tela revelada após renderização e saída no fluxo principal com desconexão do MSC antes de restaurar a console. Evidências e limites em BUILD_0.7.1.json.
