# Tab5Launcher do Paludo

[English](README.en.md) · Português

Launcher para **M5Stack Tab5 (ESP32-P4 + ESP32-C6)**, desenvolvido em C com ESP-IDF e LVGL 9. Permite instalar e iniciar firmwares, gerenciar arquivos e configurar o aparelho por toque, teclado físico e uma WebGUI responsiva.

**Versão: 0.8.0-rc.2 — Release Candidate (RC).** Esta versão será utilizada e observada por um bom tempo antes de ser considerada final. Não há data definida para a versão final. Correções encontradas durante esse período poderão gerar novos RCs. Testes automatizados aprovados não significam que todos os cenários já foram validados fisicamente.

## Inspirações e agradecimentos

- [UNIZERSION/tab5-launcher](https://github.com/UNIZERSION/tab5-launcher): inspiração para o visual do Tab5, organização dos menus, paleta de cores, mapa gráfico da flash e experiência de uso do hardware.
- [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher): inspiração para instalação e gerenciamento de firmwares, backups, catálogos OTA e WebGUI com gerenciamento de arquivos e pastas.
- [M5Stack — Tab5](https://docs.m5stack.com/en/core/Tab5) e [Tab5 Keyboard](https://docs.m5stack.com/en/tab5/Tab5_Keyboard): documentação de hardware, exemplos e suporte de placa.
- [Espressif ESP-IDF](https://github.com/espressif/esp-idf), [ESP-Hosted](https://github.com/espressif/esp-hosted-mcu), [ESP-USB](https://github.com/espressif/esp-usb) e [LVGL](https://github.com/lvgl/lvgl): base técnica e bibliotecas utilizadas.

Essas referências orientaram o desenvolvimento e os requisitos de interface. Este projeto é independente; não é uma versão oficial nem tem endosso dos projetos citados. Licenças e créditos de componentes e fontes são preservados; veja [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Recursos

- HOME com aplicativos instalados, nomes/ícones personalizados, boot padrão e desinstalação com backup opcional.
- Instalação pelo microSD, pendrive USB-A ou download OTA, com análise da imagem, verificação de integridade e progresso.
- Alocação de flash por tamanho real, mapa gráfico e reorganização com backup opcional e guarda de recuperação; até 16 aplicativos, quando houver espaço.
- Gerenciador de arquivos: microSD como **C:** e pendrive FAT/exFAT como **D:**, pastas, cópia, renomeação, edição limitada e exclusão recursiva com confirmação.
- WebGUI com sessão por cookie, arrastar e soltar arquivos/pastas, progresso por arquivo, reenvio de falhas, criação de pastas e atualização da listagem. HTML/CSS/JS ficam no microSD.
- Wi-Fi com redes salvas no cartão; temas, grade de 144 cores, wallpaper, relógios de descanso, data em português, teclado físico, brilho e configurações de energia.
- Modo exclusivo de disco USB-C para acessar o microSD no computador. Exige reinício controlado e ejeção no computador; não funciona simultaneamente com o launcher.
- Console USB, diagnósticos, perfis de dados de aplicativos e integração opcional Pi/Tab5Link. A integração Pi ainda precisa de validação física.

Backup integral é opcional e fica desmarcado na instalação, reorganização e restauração, inclusive quando já existem apps. Sem backup, mudanças que movem apps usam uma cópia temporária apenas dos bytes ocupados, verificada por hash, mantida para retomada em caso de interrupção e removida ao concluir. O SD precisa permanecer inserido durante a operação.

CFG oferece filtro de `.bin` e pastas, ocultando nomes iniciados por ponto nesse modo. A análise verifica o binário, SHA-256 e espaço antes de exibir o aceite; não há uma espera fixa para marcar a caixa.

Validação desta revisão: compilação ESP32-P4, testes host com ASan/UBSan e limites de memória/pilha aprovados. Instalação limpa e lançamento do BasiliskII/macOS foram acompanhados pela serial. A causa da tela azul em descanso e o Meshtastic ainda exigem validação específica. Foram observados timeouts intermitentes no SD ao salvar credenciais Wi-Fi, embora leituras posteriores funcionassem.

## Sem NVS do launcher

O launcher **não inicializa, lê, grava ou importa namespaces NVS**. Configuração, Wi-Fi, metadados, guarda de boot e registros de instalação/recuperação ficam em `/launcher` no microSD. O pedido temporário para entrar em USB disco usa um marcador RTC consumido uma única vez após reinício por software.

O mapa padrão para instalações novas reserva uma partição `nvs` convencional em `0x9000` (24 KiB) para os aplicativos. O launcher continua usando somente o SD para sua própria persistência. `aurora_cfg`, `vfs` e `spiffs` não são pré-alocadas. O editor FLASH permite criar NVS, FAT, SPIFFS e LittleFS, com tamanho e offset opcional. Imagens completas podem fornecer dados para destinos compatíveis por nome, tipo e offset; bootloader e tabela de terceiros não são gravados. Um binário app-only não descreve suas partições externas.

Partições de aplicativos já existentes não são apagadas por uma atualização apenas do binário do launcher. Backups de partições de dados existentes são cópias brutas, sem abrir namespaces NVS. Snapshots lógicos de NVS de versões anteriores não são convertidos automaticamente; recapture os dados antes de depender de um novo perfil. As credenciais/preferências que já estão no SD permanecem utilizáveis; não há mais importação de configurações antigas guardadas somente em NVS. Mantenha o cartão inserido para operações persistentes. O banco de aplicativos usa o espaço contíguo até a primeira partição de dados, preservando o coredump ao final da flash. Espaços livres adicionais podem receber partições de dados pelo editor.

## Compilar e testar

Use **ESP-IDF 5.4.4**, com ferramentas para ESP32-P4, Python, CMake e compilador C/C++ para os testes host. Dependências estão fixadas em [main/idf_component.yml](main/idf_component.yml).

```sh
git clone https://github.com/tpklge/Tab5Launcher_do_Paludo.git
cd Tab5Launcher_do_Paludo
# Ative o ESP-IDF 5.4.4 com o export.sh da sua instalação.
idf.py build
python tools/check_startup_memory.py
python tools/check_ui_stack.py
python tools/check_no_nvs.py
python tests/run_all.py
python tools/package_firmware.py
```

Testes host também precisam dos headers/biblioteca OpenSSL (por exemplo, `libssl-dev` no Linux ou `openssl@3` no Homebrew). Os testes de UI usam o código LVGL baixado na primeira compilação. A CI executa build, verificações de memória e regressões. Correções de compatibilidade dos componentes fixados são aplicadas durante a configuração; o driver MSC corrigido tem uma cópia local explícita.

## Cartão e WebGUI

Copie o conteúdo de [sdcard/](sdcard/) para a raiz do microSD, preservando sua pasta `/launcher` existente e suas configurações. A estrutura fornecida contém WebGUI, fontes opcionais, firmware do C6 e o exemplo de fontes OTA. O C6 não é atualizado automaticamente durante a compilação ou a cópia dos arquivos.

Edite `/launcher/ota_sources.json` manualmente para configurar as origens. Conecte o Wi-Fi e inicie a WebGUI no aparelho; use o endereço e o código exibidos pelo launcher. O navegador mantém a sessão em cookie. Configurações privadas, backups de aparelho e credenciais não fazem parte do repositório.

## Instalação e atualização

O empacotador gera arquivos em `dist/`, com offsets, tamanhos e hashes em `manifest.json` e `SHA256SUMS`.

- **Instalação inicial:** `tab5-launcher-initial.bin` é uma imagem completa para gravar a partir de `0x0`. Pode substituir o conteúdo existente da flash; faça backup integral antes.
- **Atualização de um launcher existente:** o binário do app só pode ser gravado no offset e na reserva do launcher conferidos na tabela instalada. **Não grave a tabela de `build/` nem use `idf.py flash` sobre um aparelho com mapa dinâmico existente.**
- Se o novo launcher não couber, use [tools/prepare_dynamic_upgrade.py](tools/prepare_dynamic_upgrade.py) com um backup integral recente e confira o plano. A ferramenta prepara os arquivos; não grava o aparelho.

Uma atualização apenas do app não remove entradas NVS legadas da tabela instalada. A NVS convencional do novo mapa só é criada em instalações limpas ou pelo editor de partições. Não faça atualização com instalação, restauração ou troca de perfil pendente.

## Estado de validação

Na 0.7.4, criação de 13 pastas no pendrive, leitura/escrita de 256 KiB com SHA-256 e limpeza dos arquivos de teste passaram no Tab5 sem reinício. A 0.8.0-rc.2 teve instalação limpa, boot, Wi-Fi, leitura do SD e mapa/API validados no aparelho, além do build e testes host. Recuperação de operações, perfis e observação prolongada continuam pendentes.

Limitações, pendências e registros históricos: [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md), [docs/CHANGES_0.8.0-rc.2.md](docs/CHANGES_0.8.0-rc.2.md) e [docs/TEST_PLAN.md](docs/TEST_PLAN.md). Não há garantia de execução de qualquer `.bin`, resistência a todo corte de energia ou isolamento entre firmwares que acessam a mesma flash. Sem suporte a NTFS, múltiplos pendrives/hub ou Secure Boot/flash criptografada neste fluxo de instalação.

## Licença

[GNU GPL v3](LICENSE). Componentes de terceiros e fontes mantêm suas próprias licenças e avisos.
