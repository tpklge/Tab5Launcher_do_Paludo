# 0.8.0-rc.2 — mapa limpo

[Remoção das reservas de dados e gráfico fiel](CHANGES_0.8.0-rc.2.md). Build e testes aprovados. Apagamento integral autorizado pelo usuário; SD e pendrive preservados. Ampliação da fronteira do alocador continua fora desta revisão.

# RC preparado — 0.8.0-rc.1

[Remoção de NVS e publicação bilíngue](CHANGES_0.8.0-rc.1.md). Build, ausência de rotinas NVS e regressões aprovados; sem flash. Validar fisicamente: salvar configurações/Wi-Fi e reiniciar; iniciar/retornar de apps; modo USB uma única vez, cancelamento por reset físico; recuperação de operação interrompida; falha de SD sem escrita em flash NVS; perfis brutos e recaptura de snapshots legados. Uso prolongado antes da versão final.

# Revisão instalada — 0.7.4

[USB MSC corrigido](CHANGES_0.7.4.md), build e suíte host aprovados, flash apenas do app com hash verificado. Teste físico no D: criou 13 pastas, verificou 256 KiB por SHA-256 e removeu os dados de teste sem reinício. [Registro](BUILD_0.7.4.json). Ainda faltam ensaios prolongados e os cenários específicos de recuperação descritos abaixo.

# Revisão preparada — 0.7.3

Cinco correções críticas e regressões concluídas, sem flash: [detalhes](CHANGES_0.7.3.md) e [evidências](BUILD_0.7.3.json). No aparelho, validar recuperação de configuração, edição com arquivo auxiliar existente, recusa de boot com instalação pendente e transições USB/boot/desligamento durante upload, sem remover o cartão durante escrita. Cortes de energia exigem ambiente de recuperação e backup; não foram ensaiados.

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

# Correção SD — 0.2.1

A serial identificou `Stack protection fault` em `taskLVGL`, na função
`ui_sd.c:submit`, ao abrir o SD. O pedido contém campos de edição e plano de
instalação, cerca de 13 KB, e estava como variável local numa pilha LVGL de 7 KB.

Agora o pedido é alocado dinamicamente, copiado pela fila e liberado tanto no
sucesso quanto na falha. Falta de memória retorna erro visível, sem deixar busy
travado. A função `submit` usa **32 bytes** de frame no build ESP32-P4. Worker SD
continua com pilha própria de 48 KB; LVGL recebe 12 KB para margem dos diálogos.

Regressão: `.su` do compilador e `tools/check_ui_stack.py` limitam frames não-worker
(3 KB; submit SD 512 bytes); o empacotamento também executa essa verificação.
Teste host injeta falha de alocação e repete 100 ciclos de tela com ASan/UBSan.
SD e navegação passaram novamente.

Teste físico diagnóstico: cinco visitas ao SD alternadas com HOME concluídas sem
panic/reinício; listagem real de 31 entradas, página de 24, em ~250 ms. Margem mínima
observada na pilha anterior de 7 KB: 1524 bytes. Nenhum arquivo/app foi gravado por
esse teste. A versão final **0.2.1** foi gravada somente em 0x20000, hash verificado, com autoteste desabilitado e pilha LVGL de 12 KB. Serial confirmou 0.2.1 pronta, sem panic no boot. Apps/tabela/dados preservados; monitor em `/tmp/tab5-021-monitor.log`.

---

# Flash 0.2.0 e correção de memória no boot

Backup atual completo de 16 MB em `recovery/tab5-before-0.2.0.bin`, com SHA/partições
no manifesto ao lado. Gravados somente bootloader 0x2000 e launcher 0x20000;
hashes verificados. Tabela, NVS, app0/app1 e demais dados preservados.

A primeira gravação expôs falta de RAM interna antes de `app_main`: alocação da
pilha principal (8704 bytes) falhou com apenas 5992 bytes internos disponíveis.
O teste com o loader anterior reproduziu a falha, isolando o problema no app.
Caches estáticos da UI agora ficam na PSRAM (EXT_RAM_BSS_ATTR), mantendo transporte
ESP-Hosted e DMA internos. Diagnóstico de falha de alocação fica registrado no boot.
`tools/check_startup_memory.py` verifica a colocação no ELF e evita regressão.

Após regravar app corrigido e bootloader novo, serial confirmou 0.2.0, startup
com 156172 bytes internos livres, launcher pronto com 290703 bytes internos,
UI 1280×720, inventário de dois slots, INA226 8273 mV / estimativa 93% e hora do RTC.
Sem assert/falha de alocação no boot de verificação. Rede Wi-Fi salva não encontrada no período observado (reason 201/NO_AP_FOUND); credenciais não apagadas. Testes HOME/SD com ASan/UBSan
passaram novamente. Recursos novos ainda precisam de interação/ensaio físico.

O monitor atual escreve `/tmp/tab5-020-fixed-monitor.log`, aberto sem reset adicional.

---

# Revisão 0.2.0 — seis primeiros grupos

**11/11/2026.** Pedido: implementar SD, instalação, HOME, boot, relógio/energia e visual.
Implementação/uso/limites em [SIX_STAGES.md](SIX_STAGES.md). Esta revisão ainda não
foi gravada no Tab5. A confirmação de ambos os apps iniciando pertence ao firmware
anterior e permanece registrada abaixo.

- SD consulta FATFS, distingue desconhecido de zero, ordena globalmente, copia
  pastas, edita texto com revalidação e `.previous`, restaura app/dados pelo seletor.
- Instalador aceita merged validado e manifesto multipartes próprio com app/dados;
  backup precede substituição inclusive de slot incompleto. Source/destino rechecados.
- Perfil 2×4 → 1×8 MB: backup completo verificado no SD, intenção/espelho da tabela,
  capacidade nova do bootloader obrigatória e arquivo/hash de retomada em NVS.
- HOME guarda nomes/ícones pela identidade da imagem, configura padrão/reativação.
- Autoboot conectado à UI; modo manual, retorno/deep sleep e falhas via NVS.
- RTC RX8130CE UTC/ajuste offline, controle de carga e desligamento com confirmação.
- Temas aplicados ao conjunto de telas; PNG/JPEG/blur em worker; quatro relógios,
  dim/off e primeiro evento consumido no despertar. Sem descanso em operação crítica.
- NVS indisponível não provoca apagamento automático; preferências NVS prevalecem
  sobre configuração inicial do SD. Catálogo/WebUI/USB/Pi permanecem posteriores.

## Verificação

`python tests/run_all.py` passou: parsers, instalador/restauração, filesystem,
registro, RTC, bootloader e runtime, modelo/transação de layout, Wi-Fi e LVGL real
(HOME/SD/Wi-Fi/formulários/configurações/PNG/JPEG/blur/status/relógios). ASan/UBSan
onde indicado nos testes. Falhas injetadas incluem tabela parcial/espelho inválido,
erase/write, retenção de backup e destino alterado. Imagem sintética de 5 MB também
passou pelo pipeline de streaming <=4 KB em slot simulado de 8 MB.

Build ESP-IDF 5.4.4 **0.2.0** aprovado: launcher 1,504,016 bytes e bootloader 21,200 bytes. Pacote e SHA256SUMS gerados/verificados em dist; manifesto de validação em BUILD_0.2.0.json. Testes de hardware
pendentes: RTC retendo hora, carga/power-off, novo espaço livre, restore/pacotes,
boot padrão e wallpaper prolongado. Testes simulados não comprovam resistência
física a falta de energia. Nenhum flash foi executado nesta etapa.

---

## Histórico anterior

# Progresso / apps / teclado / energia — 11/11/2026

## Três etapas implementadas

1. **Instalação e apps:** overlay com barra, percentual e etapa; HOME assíncrona
   enumera todos os slots; layout com dois slots de 4 MB. Desinstalar pede
   confirmação, confere digest e grava backup integral verificado no SD antes
   de apagar. Cancelamento permitido antes do erase; após iniciar, termina o
   slot. Arquivos no SD e partições de dados não são removidos.
2. **Teclado:** indev LVGL real com press/release. Enter aciona botões; Tab,
   Shift+Tab e setas navegam; Esc/F6 retorna ao menu lateral. Alt+1..5 abre HOME,
   SD, Wi-Fi, CFG e DIAG. Modais isolam o foco; Escape cancela. Campos mantêm
   entrada física e teclado virtual opcional.
3. **Energia/status:** worker INA226 em I2C 0x41, leitura de tensão/corrente com
   sinal, estados carga/descarga/repouso. Percentual aproximado por tensão de
   bateria 2S (`~%`), sem alegar fuel gauge ou presença USB por corrente zero.
   Barra inclui Wi-Fi, espaço livre SD e teclado; DIAG inclui valores brutos.

## Verificado nesta revisão

- ESP-IDF 5.4.4: build concluído, launcher **1.400.976 bytes**; pacote/hash em dist.
- ASan/UBSan: instalador/desinstalador, boot único, Wi-Fi, UI LVGL real de HOME,
  SD, Wi-Fi e navegação. Enter realmente gera CLICKED; checkbox permite alcançar
  Instalar; 100 ciclos de diálogos/foco/progresso e telas sem erro detectado.
- Parser: binário real e 1500 mutações (UBSan). Power model: unidades/sinal,
  estimativa e leitura indisponível. Preflight: checksum/tabela/dados preservados.
- Backup completo **16 MB** em `recovery/tab5-before-three-stages.bin` antes do
  flash. SHA e relatório em `recovery/multiboot-preflight.json`.
- Imagem existente de **891.584 bytes** cabe em 4 MB e metade superior estava
  integralmente FF. Tabela alterada apenas no tamanho de app0 e acréscimo de app1;
  todos os endereços das partições de dados mantidos.
- Gravados **somente 0x8000 (tabela) e 0x20000 (launcher)**; hashes verificados.
  Bootloader, pedido de boot, NVS, app instalado e dados não foram gravados.
- Serial após reset de verificação: factory launcher, política one-shot ready,
  **Inventory: 2 OTA slots**, A164 HID, **INA226 8291 mV / 1 mA / estimativa 94%**,
  UI 1280x720 pronta e Wi-Fi reconectado automaticamente. Primeiro boot após
  flash recuperou AUTH_EXPIRE via retry e obteve IP. Sem panic observado nesse
  intervalo; não se afirma estabilidade prolongada.

## Retorno do usuário após esta gravação

Usuário confirmou que **ambos os aplicativos iniciam normalmente**. Também
relatou espaço livre SD em **0.0 GB**. Causa confirmada no código:
`board_sd_free_bytes()` ainda retorna zero fixo; a barra não estava lendo o
espaço livre real. Falta implementar consulta FATFS e distinguir falha de
consulta de volume cheio. Nenhuma correção/novo flash foi feita nesta consulta.

## Validação física restante

Barra durante nova instalação, navegação física completa, desinstalação e
variação de carga/descarga ainda precisam de confirmação explícita no aparelho.
Inicialização dos dois apps foi confirmada pelo usuário. O app anterior substituído pelo layout de um slot não é
restaurado automaticamente: reinstale seu `.bin` do SD para ocupar app1.
O app residente mais recente foi preservado. Imagens acima de 4 MB exigirão um
layout futuro apropriado; migração genérica/merged continuam pendentes.

Referência do monitor de energia: implementação oficial
[M5Unified Power](https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.inl)
e [INA226](https://github.com/m5stack/M5Unified/blob/master/src/utility/power/INA226_Class.inl),
consultadas em 11/11/2026. Código de decodificação e worker próprios.

---

## Histórico da revisão anterior

# SD / instalação app-only / retorno — concluído nesta revisão

**Data:** 11/11/2026. Prioridade confirmada pelo usuário: SD e instalação de firmware.

## Implementado

- Menu SD com placas escuras/destaque vermelho, painel de detalhes, breadcrumb,
  paginação de 24 entradas, arquivos/pastas, seleção por toque e pasta por toque longo.
- Worker único para listar, criar pasta, copiar com cancelamento, renomear,
  excluir arquivo/pasta vazia e prévia de texto de 4 KB. Nenhum acesso lento em
  callback LVGL. Filas/generation IDs protegem telas destruídas.
- Parser app-only P4 (ID correto **0x12**), revisão, descritor, limites/alinhamento,
  overflow/sobreposição, entrada, checksum e SHA-256. Pacotes/merged/backup e
  assinaturas adicionais rejeitados, sem magic scanning ou offsets adivinhados.
- Plano para slot OTA existente (atual: app0 de 8 MB), nome do app substituído,
  aceitação explícita de requisitos de dados desconhecidos; infra/dados preservados.
- Backup integral de slot substituído, relido/hash verificado e manifesto JSON;
  falha de backup impede apagar. Preflight/hash/identidade do destino rechecados,
  journal NVS, gravação de 4 KB via OTA SDK e verificação final. Mutex global.
- Bootloader com pedido CRC32 durável, consumido antes do salto. Sem pedido,
  seleciona factory. HOME e conclusão do instalador podem iniciar um app uma vez.
- `launch_req` adicionado em 0x12000/0x1000, sem mover nenhuma partição antiga.
- Dependências resolvidas fixadas e pinos SDIO explícitos, sem exigir o preset
  local alterado do ESP-Hosted. LVGL 9.3.0 mantido.
- Pacote/manifesto/SHA256SUMS e imagem inicial em `dist/`, gerados por
  `tools/package_firmware.py` a partir dos endereços do build/tabela.

## Verificação realizada

- Parser: binário real + fixtures/corrupção e 1500 mutações com UBSan.
- Arquivos: limites/paginação/nomes, integridade da cópia, cancelamento e exclusão
  não recursiva com UBSan.
- Bootloader: hook real com requests parciais/CRC inválido/slot errado, boot único,
  retorno e falha de erase com ASan/UBSan.
- Instalador real com SDK/flash simulados: backup+manifesto, fonte/destino alterados,
  cancelamento, journal/SDK falhando, alvo ativo e security gate com ASan/UBSan.
- UI LVGL real: SD e Wi-Fi, geometria, teclado, confirmações e 100 ciclos com ASan/UBSan.
- Build ESP-IDF 5.4.4 concluído; launcher ~1,34 MB, bootloader 20.784 bytes.
- Antes da gravação, backup completo de 16 MB em
  `recovery/tab5-before-sd-installer.bin`, SHA-256/offsets no manifesto ao lado.
  Comparação confirmou partições antigas idênticas e nova região fora delas.
- Gravados e verificados bootloader 0x2000, tabela 0x8000, pedido vazio 0x12000 e
  launcher 0x20000. NVS, app0 e dados preservados pela atualização.
- Serial confirmou `one-shot return policy: ready`, inicialização da factory e
  reconexão Wi-Fi. Durante teste do usuário, SDK verificou imagem no app0 e a
  serial identificou **microcity 0.11.0** executando.
- Usuário confirmou: **"SD, instalação e retorno funcionaram"** após teste com RESET.

## Limites / próximas etapas do projeto

Merged/multipartes, reparticionamento/migração automáticos, dados por app, restore
pela UI e ensaios de corte de energia continuam pendentes. O teste físico acima
não prova compatibilidade com todos os firmwares nem valida falta de energia.
Catálogo/OTA, WebUI, USB, Pi e acabamento completo de temas/RTC seguem posteriores.

Aviso LFN do BSP 1.3.2 é incompatível com os símbolos do SDK 5.4: a configuração
real está em heap, 255 caracteres (`CONFIG_FATFS_LFN_HEAP=y`). Não foi modificado
silenciosamente o componente gerenciado para esconder o aviso.

---

# Correção de continuidade e credenciais Wi-Fi — 11/11/2026

- Scan restaurava READY mesmo mantendo associação/IP: corrigido para conservar
  CONNECTED. Navegação não dispara novo scan quando a rede está conectada;
  a lista anterior permanece e a tela mostra SSID/IP ao retornar.
- Removido disconnect incondicional antes de conectar. Troca entre redes aguarda
  o evento da desconexão antiga antes de iniciar outra tentativa. Rejeição pelo
  AP não gera disconnect redundante; timeout DHCP faz cleanup com confirmação.
- Selecionar o SSID já conectado não derruba o link nem grava senha não testada.
- Credenciais agora gravadas somente após obter IP em blob NVS versionado único
  (`l5_wifi/networks_v1`), incluindo auth. Erro ao salvar é informado, preservando
  a conexão. Senhas/SSIDs não são recortados ou impressos nos logs.
- Credenciais legadas indexadas podem ser lidas com limite de 8 redes; nenhuma
  credencial é apagada por falha de conexão. Lista mantém a última rede no topo.
- Senha salva é preenchida ao selecionar novamente a rede. Quando a opção
  Configurações > Wi-Fi autoconnect está habilitada, app_main inicia restauração
  após mostrar a UI, respeitando a preferência existente.
- Queda não solicitada permite até 3 tentativas de reconexão com espera limitada;
  Desconectar desabilita essas tentativas. Logs agora registram reason do driver.
- `tests/host_wifi`: testes do código real com injeção de eventos/NVS passaram
  com ASan/UBSan. `tests/host_ui`: preservação de conexão/lista ao navegar e senha
  preenchida adicionados aos testes que passaram (100 ciclos).

Validação física desta revisão pendente da nova gravação e conexão do usuário.

---

# Revisão de UI / teclado — 11/11/2026

- Imagens HOME/CFG da UNIZERSION consultadas; implementação própria com sidebar
  de 112 px, placas escuras, destaque vermelho e HOME com cartões maiores.
- Corrigida altura inválida `LV_PCT(100) - pixels`; lista Wi-Fi ocupa a área útil.
- WPA2/WPA3 misto aceito; modos enterprise não suportados mostram explicação.
- Diálogo de 760x330 no topo; teclado virtual de 300 px abaixo, ocultável pelos
  botões de pronto/cancelar. Conectar/Cancelar ficam visíveis. Teclado virtual
  inicia oculto quando há A164; botão permite abrir mesmo assim.
- Driver A164 corrigido conforme registradores oficiais: 0x10 modo, 0x02 contagem,
  0x30 HID, 0x00/0x01 interrupção. Confirma identidade em 0xFF. Poll 20 ms.
- Integrada fila de teclas na UI: senha, espaço/símbolos, cursor, Enter/Esc e foco.
  Recovery I²C agora pertence ao worker, evitando remoção concorrente do device.
- LVGL fixado em 9.3.0; o intervalo anterior instalara 9.6.0. Assert de alocação
  e ponteiros habilitados. Navegação diferida e callbacks Wi-Fi passam por fila;
  apenas a tarefa LVGL cria/atualiza/remove objetos Wi-Fi.
- Testes host com LVGL real: geometria, senha, WPA2/WPA3, teclado físico/virtual,
  respostas atrasadas e 100 ciclos create/delete; passaram com ASan/UBSan.
- Build ESP-IDF 5.4.4 concluído. App de 1.285.568 bytes gravado em 0x20000,
  hash verificado, NVS/tabela preservados. Serial confirmou scan ESP_OK/16 redes.
- Conexão autenticada e ausência de novas telas azuis aguardam confirmação física.

Veja `docs/UI_REVIEW.md` e `tests/host_ui/README.md` para referências, renders e
reprodução dos testes. Monitor serial permanece aberto.

---

# Revisão de código — 11/11/2026

O registro histórico abaixo descreve a intenção inicial e contém afirmações superadas.
Esta seção registra o estado verificado na revisão.

- Não foi encontrado `prompt_claude.md`. Foi lido `prompt_launcher_tab5.md`.
- Projeto local sem repositório Git; não há histórico de commits disponível.
- Build local: ESP-IDF **5.4.4**, alvo ESP32-P4; dependências em `dependencies.lock`.
- Corrigidos botões das redes Wi-Fi sem callback, SSIDs truncados por espaços,
  autenticação fixada em WPA2, teclado virtual órfão e callbacks acessando telas destruídas.
- Adicionados resultado de conexão por DHCP/desconexão e timeout no worker; evento
  padrão criado antes de registrar ESP-Hosted. Não há validação física de conectividade.
- Protegidas APIs Wi-Fi antes da inicialização, falhas de alocação, SSIDs com 32 bytes,
  sobreposição de scans e limites do array de credenciais carregadas do NVS.
- Corrigidos timer de boot sobrevivendo à tela e segundo extra na contagem;
  callback da barra de estado agora possui a assinatura LVGL correta.
- Boot valida índice de slot e seleciona a partição antes de desligar periféricos;
  retorno verifica a seleção da factory em vez de ignorar erro ao apagar otadata.
- Importação lê o caminho informado e propaga erro de leitura. Escritas NVS e falhas
  de finalização do arquivo SD não são mais ignoradas. Diretórios longos não são truncados.
- Inicialização repetida de SNTP protegida.
- Regressão host: `tests/test_storage_host.py` compila o componente real com doubles
  NVS/FreeRTOS e ASan/UBSan; verifica importação, caminho ausente, linha vazia e falha NVS.
  Neste macOS, executar com `SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk
  python3 tests/test_storage_host.py` (SDK padrão 27 incompatível com o linker instalado).

## Pendências materiais

Instalador/parser, catálogo, backup, WebUI, USB e Pi seguem stubs. Não há bootloader
customizado: RESET de um app OTA continua nesse app; o guard local não fornece proteção
entre firmwares. Autoboot ainda não está conectado à inicialização da UI. RTC físico e
bateria não estão implementados. Recovery Wi-Fi, persistência de novas credenciais,
reconexão automática e integração do teclado com LVGL ainda exigem trabalho.
O parser de configuração aceita apenas o formato simples por linhas; validação completa
de JSON/esquema e sincronização de todos os acessos mutáveis continuam pendentes.
Não foi gravado nenhum dispositivo. Próximo passo: completar testes host dos módulos e
validar boot offline, navegação repetida e conexão/desconexão no Tab5 antes de instalar apps.

---

# Tab5 Launcher — Progress

## Current State: Etapa 1 Complete

**Date:** 2026-11-11

### What was implemented in Etapa 1

**Project structure:**
- Full ESP-IDF/CMake project (`CMakeLists.txt`, `main/`, 16 components)
- `sdkconfig.defaults` for Tab5 (ESP32-P4, PSRAM, LVGL, ESP-Hosted 2.12.6, BSP 1.3.2)
- `partitions.csv` — Default "2 Apps" scheme (launcher factory + app0 8 MB + aurora_cfg preserved)
- `main/idf_component.yml` — pinned versions (IDF 5.5.2, BSP 1.3.2, LVGL 9.3.0, ESP-Hosted 2.12.6)

**Components with real code:**
- `board` — PI4IOE expander, display (BSP), SD card, RTC, battery, charger, C6 power, USB5V
- `boot_mgr` — NVS-backed default slot, countdown, boot-loop guard (RTC memory), launch, return-to-launcher
- `input` — Tab5 Keyboard A164: own I²C bus (GPIO0/1/50), HID mode, ISR + worker, hot-plug, event queue, recovery
- `wifi` — 11-state machine, ESP-Hosted async init, C6 power, 6-level recovery, saved networks in NVS
- `storage` — NVS namespace "launcher_cfg", SD config.conf overlay, schema defaults, timezone
- `time_mgr` — RTC → system clock, TZ (America/Cuiaba UTC-4 default), NTP async
- `ui` — LVGL side menu, app carousel (HOME), boot countdown screen, status bar, 4 themes
- `diagnostics` — reset reason, heap snapshot, circular log

**Stubs (compile cleanly, implemented in Etapa 2+):**
- `firmware`, `installer`, `partitions_mgr`, `catalog`, `backup`, `web`, `usb`, `pi_link`

**Documentation:**
- `docs/FEATURE_PARITY.md` — full matrix of features from both reference projects
- `docs/ARCHITECTURE.md` — component graph, bus owners, task model, init order
- `docs/HARDWARE.md` — confirmed pin map, I²C devices, revision table, PI4IOE bits
- `docs/FLASH_LAYOUT.md` — partition table with offsets, return-to-launcher strategy

### Version decisions confirmed from UNIZERSION audit

| Dependency           | Version  | Reason                                               |
|----------------------|----------|------------------------------------------------------|
| ESP-IDF              | 5.5.2    | Used by UNIZERSION; proven stable on Tab5 P4         |
| M5Stack Tab5 BSP     | 1.3.2    | Same as UNIZERSION                                   |
| LVGL                 | 9.3.0    | Same as UNIZERSION                                   |
| esp_lvgl_port        | 2.8.0    | Same as UNIZERSION                                   |
| ESP-Hosted           | 2.12.6   | C6 ships 2.12.6 on later units; early = 1.4.1        |
| esp_wifi_remote      | 1.5.1    | Required by ESP-Hosted >= 2.11                       |

### Known risks and limitations (Etapa 1)

1. **IDF 5.5.2 is marked experimental for ESP32-P4**: CONFIG_IDF_EXPERIMENTAL_FEATURES required.
2. **BSP 1.3.2 API assumed**: `bsp_rtc_init`, `bsp_rtc_time_valid`, `bsp_battery_get_voltage_mv`,
   `bsp_battery_get_current_ma` are assumed to exist — verify against actual BSP source before Etapa 2.
3. **ESP-Hosted 2.12.6 vs 1.4.1**: Early Tab5 units have 1.4.1 on C6; detection and fallback needed in Etapa 2.
4. **board.c uses `bsp_display_get_panel_io_handle()` for revision detection**: confirm this API exists in BSP 1.3.2.
5. **ui.c uses C++ lambda in C file**: `lv_obj_add_event_cb` callback written as lambda — must be rewritten as named function for C99 compliance.
6. **No actual build test**: Build has not been run on a machine with IDF 5.5.2 installed. CMake structure is correct but API mismatches may surface.

---

## Next: Etapa 2

**Goal:** BSP integration working, full UI functional, Wi-Fi with recovery, keyboard in all screens.

**Checklist:**
- [ ] Verify BSP API names against `espressif/m5stack_tab5 1.3.2` source
- [ ] Fix C99 compatibility issues (remove lambdas in `ui.c`)
- [ ] Run `idf.py build` and fix compilation errors
- [ ] Implement LVGL keyboard indev (send keyboard events to focused LVGL widget)
- [ ] Implement wallpaper loading from SD (async, JPEG/PNG)
- [ ] Implement live theme switch
- [ ] Implement idle clock overlay
- [ ] Implement Wi-Fi UI (scan list, connect dialog, recovery buttons)
- [ ] Implement time entry screen
- [ ] Implement SD file browser
- [ ] Implement diagnostics screen
- [ ] Test boot sequence on hardware
- [ ] Verify keyboard detection, hot-plug, HID mode
- [ ] Verify ESP-Hosted init and C6 version detection

## Diagnóstico no dispositivo — transporte C6

Após flash, serial confirmou tela/touch ST7123, PSRAM 32 MB e teclado HID.
Scan falhou com `sdmmc_init_ocr ... 0x107` e timeout ESP-Hosted.
Identificada configuração de reset invertida: neste driver, `ACTIVE_HIGH` é o
nível final HIGH após pulso LOW; `ACTIVE_LOW` deixa GPIO15 LOW ao terminar.
A configuração anterior mantinha o C6 em reset. Corrigidos sdkconfig e defaults
explicitamente, sem depender do default alterado no Kconfig da dependência local.
Backlight mantido apagado até renderizar a primeira tela para evitar fundo inicial.
Relato de reinício/tela azul do usuário ainda sem causa confirmada por backtrace.
Também desativado `ESP_HOSTED_TRANSPORT_RESTART_ON_FAILURE` para que falha do
transporte não solicite reinício automático do P4. A tela Wi-Fi agora apresenta
erros de inicialização e solicita scan quando o driver sinaliza READY.

Validação após corrigir GPIO15: `Card init success`, ESP-Hosted transport UP,
Wi-Fi READY e dois eventos StaScanDone no aparelho. Identificada diferença real
host 2.13.0 / C6 2.12.0 (sem atualizar C6 nesta revisão).
Capturado panic `lv_event_mark_deleted` durante navegação. Corrigida chamada
inicial de rotação sem lock após iniciar a tarefa LVGL; leitura de touch também
passou a usar o lock. A ausência de novos panics requer teste repetido no aparelho.
Última imagem gravada somente em 0x20000, hash verificado. Após correção de lock,
serial confirmou `scan complete: ESP_OK, 16 networks` no aparelho. Monitor aberto
sem reset adicional (`esp_idf_monitor --no-reset`). Teste de navegação repetida
solicitado ao usuário; estabilidade prolongada e conexão autenticada pendentes.

Confirmação física do usuário após esta gravação: **"Digitou e conectou"**
no teste de selecionar rede, digitar senha no A164 e conectar. Sem nova tela azul
relatada nesse teste. Isso não substitui teste prolongado de navegação/hot-plug.

Nova imagem de 1.291.008 bytes gravada somente em 0x20000; hash verificado.
Monitor serial aberto sem reset adicional. Solicitado teste físico de primeira
conexão/salvamento, navegação e segunda conexão sem redigitar senha.

Serial após nova conexão: IP 192.168.88.17 e `wifi_link: connected;
credentials committed to NVS` aos ~85 s de uptime. Gravação de credenciais
confirmada no aparelho. Teste de navegação/segunda conexão ainda em andamento.

Confirmação do usuário: primeira conexão funcionou e senha permaneceu salva;
após reiniciar houve `reason=2` (AUTH_EXPIRE), mas uma tentativa posterior conectou.
Serial confirmou restauração da rede salva no boot e IP novamente obtido aos ~62 s.
Também foi observado `Brownout detector was triggered` antes de um reinício;
não foi desativada a proteção nem atribuída causa física sem medição.
Adicionado retry limitado para AUTH_EXPIRE: 3 tentativas totais, intervalos de
10 e 20 s, estado visível na UI; outras rejeições não disparam esse retry.
Teste host verifica recuperação após dois AUTH_EXPIRE, limite de tentativas e
preservação das credenciais quando todas falham. Passou com ASan/UBSan.

Validação física da última imagem (1.291.472 bytes, gravada em 0x20000, hash OK):
primeira tentativa no boot falhou com AUTH_EXPIRE/reason 2 aos 10,9 s; firmware
agendou retry 2/3 em 10 s. Segunda tentativa iniciou aos 20,9 s, associou aos
21,5 s e obteve IP 192.168.88.17 aos 22,5 s, sem entrada manual de senha.
Credenciais confirmadas no NVS novamente. Recuperação automática desse caso
foi observada no aparelho; monitor serial continua aberto.

## 0.4.0 — 11/11/2026

Transferência de diretórios/sessão/feedback WebGUI, cache e detalhes OTA,
seletores de imagem, data/hora brasileira, confirmação de desligar, ações SD,
fontes latinas, backups de dados na remoção e editor visual delimitado aos apps.
Ver [CHANGES_0.4.0.md](CHANGES_0.4.0.md) para comportamento, testes e pendências.

## 0.5.0 — migração, perfis e OTA

Itens 1 a 3 implementados: migração dos slots preservando todos os apps que caibam;
backup/restauração integral compatível na tela e utilitário ROM USB para outras
revisões; perfis FAT/SPIFFS/NVS por app com captura/retomada/rollback; downloads
Range/ETag e consulta/download de candidatos em lote. Contratos e validação em
CHANGES_0.5.0.md e BUILD_0.5.0.json.

## 0.6.0 — WebGUI e itens 4 a 7

Gerenciador web, MSC exclusivo, fontes/imagens, serviços Pi, calibração e C6.
Detalhes em CHANGES_0.6.0.md; evidências e limitações físicas em BUILD_0.6.0.json.

Validação 0.6.0 no aparelho: arquivos web/fontes/C6 copiados e relidos pelo MSC;
ejeção e retorno por software confirmados; HTTP real (30 uploads, lista completa,
edição Unicode, rename/copy e exclusão recursiva); navegador real em quatro
larguras; C6 atualizado 2.12.6 → 2.13.1 com reconexão. Observação final de 300 s,
30 amostras, sem erros/reinícios, SD e Wi-Fi disponíveis. Não equivale a ensaio
prolongado, teste de autonomia nem validação Pi. Pendrive USB-A permanece pendente.

## 0.7.0 — alocação automática, USB-A e energia

Implementação descrita em CHANGES_0.7.0.md; fontes OTA no SD, detalhes laterais,
cores 12×12, backup opcional, uploads com substituição e diretórios próprios em /launcher.
Evidências físicas em BUILD_0.7.0.json.


Pedidos adicionais de novasfuncionalidades.md incluídos na 0.7.0: mapa gráfico manual no Tab5/Web, ações laterais, C:/D: ampliados e revisão de RAM/toque. A fila SD agora transfere ponteiros de cargas na PSRAM e os workers são criados antes do Wi-Fi. Validação física: 25 trocas de telas e dois mapas manuais aplicados com reinício normal; testes de host completos aprovados. aurora_cfg está vazia e é mantida por compatibilidade; configurações próprias já ficam no cartão. Detalhes finais em BUILD_0.7.0.json.


## 0.7.1 — USB, descanso e cinco temas

MSC com buffer de 2 KiB, enumeração antes da tela e backlight retido durante reinícios controlados, liberado após inicialização em zero; primeiro quadro mostrado com board_reveal. O boot continua exclusivo. Resultados de scan Wi-Fi na PSRAM compensam o buffer USB, mantendo o limite de DRAM pré-scheduler (0x4ff1edb0). Descanso com dia da semana e data por extenso em português, texto ampliado; cinco temas novos disponíveis nativamente e na WebGUI. Regressões de host aprovadas, incluindo nove temas e calendário/acento. Evidências físicas desta revisão em BUILD_0.7.1.json.


## 0.7.2 preparada — observação de descanso

Relato do usuário: falta de resposta com hora avançando, recuperada após RESET. Reproduzido em host LVGL o timer de RELEASED pausado após despertar em modo EVENT. Ajuste passou em 16 casos de ponteiro e 100 ciclos; texto inalterado deixa de invalidar a tela. Compilação/orçamentos aprovados. Ainda não gravado para preservar a observação da 0.7.1 solicitada pelo usuário. Ver CHANGES_0.7.2.md e BUILD_0.7.2.json.
