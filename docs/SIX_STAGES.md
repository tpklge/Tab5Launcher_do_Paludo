# Seis grupos implementados — revisão 0.2.0

Esta revisão foi compilada/testada em host e gravada no Tab5. Após corrigir o
orçamento de RAM interna no boot, serial confirmou launcher pronto, dois slots,
RTC e INA226. Os novos fluxos ainda precisam de interação física. A confirmação
anterior de ambos os apps iniciando pertence à revisão física anterior.

| Grupo | Entrega | Limites deliberados |
|---|---|---|
| SD | FATFS informa espaço livre real; ordenação global; copiar pastas; editar texto; restaurar backups pelo seletor | 16.384 entradas por pasta; cópia até 16 níveis/100.000 itens; texto até 4095 bytes; excluir pasta continua exigindo que esteja vazia |
| Instalação | App-only, merged com tabela verificada, manifesto multipartes, app/dados com backup, perfil automático 8 MB e retomada | P4; dados `vfs`/`spiffs`; formatos abaixo; maior app 8 MB; perfil 2×4 → 1×8 MB; sem reparticionamento arbitrário |
| HOME | Nome personalizado, ícone PNG/JPEG assíncrono, app padrão e reativação após bloqueio | Registro NVS vinculado ao SHA da imagem; ícones até 256×256 |
| Boot | Contagem funcional, início somente por botão, supressão ao retornar de app/deep sleep configurado, contador persistente de falhas | Três falhas consecutivas por slot; não se infere duração entre boots usando uptime de outro firmware |
| Relógio/energia | RX8130CE UTC, data/hora offline, fuso POSIX, habilitar/desabilitar carga, desligamento com confirmação | 2000–2099; USB pode manter alimentação; teste físico dos novos controles pendente |
| Visual | Quatro temas aplicados às telas, destaque, wallpaper, quatro relógios, fundos desfocado/transparente/preto, dim/off, primeiro evento apenas desperta | Wallpaper até 1280×720/4 MB; JPEG baseline/PNG; fallback quando ausente/inválido |

## Uso

- **SD:** escolha um arquivo e use **Editar**, ou selecione uma pasta por toque
  longo e use **Copiar**. A colagem não sobrescreve destinos existentes. Ordenação
  ocorre antes da paginação, com pastas primeiro e nomes sem distinguir caixa.
- O editor carrega somente texto completo de até 4095 bytes. Texto maior fica em
  prévia somente leitura. Antes de publicar, revalida o conteúdo original, grava
  e sincroniza um temporário e conserva a versão anterior como `.previous`.
  FAT não oferece rename sobre destino existente: a publicação usa duas renomeações,
  não uma transação de energia. Se interrompida, procure `.previous`/`.edit-part`.
- **Restaurar:** selecione o `.raw` acompanhado do `.raw.json` original; use
  **Analisar / restaurar** e confira o destino. Backup de dados restaura somente
  a partição indicada. Backup de app prefere o slot de origem; se removido do
  layout, pode usar outro slot compatível, sempre mostrado antes da confirmação.
  Backup completo de 16 MB continua tendo restauração externa.
- **HOME:** Renomear, Escolher ícone, Usar no boot/Tirar padrão e Desinstalar.
  Ícone automático: `/sdcard/apps/<projeto original>/icon.png`. Renomear não muda
  a identidade da imagem nem a pasta automática. Arquivo inválido mantém ícone genérico.
- **CFG:** tema, destaque hexadecimal, wallpaper, intensidade, relógio, brilho,
  tempos de descanso, contagem, início manual, deep sleep, data/hora e carga.
  Desligar exige digitar `DESLIGAR`. Arquivos/flash ativos bloqueiam o corte de energia.
  Preferências internas NVS prevalecem sobre o arquivo SD usado como configuração inicial.
- O relógio aparece no tempo de redução de brilho. O desligamento da tela segue
  seu tempo próprio. Tecla ou toque que desperta o overlay é consumido. Operações
  de instalação/arquivos impedem descanso automático.

## Perfil de 8 MB e recuperação da tabela

Quando nenhum slot comporta o app e o layout reconhecido é 2×4 MB, o plano pode
oferecer **1×8 MB**. `app0` permanece em 0x210000 e passa a terminar em 0xA10000;
`app1` deixa de existir. NVS, launcher, VFS, `aurora_cfg`, SPIFFS e coredump não mudam.
O plano informa a remoção do segundo slot; não promete preservar dois apps nesse perfil.

Antes de alterar a tabela, grava e relê um backup completo de 16 MB no SD em
`/backups/layout-before.bin`, acompanhado de SHA-256. Uma cópia anterior com esse
nome não é sobrescrita: mova-a antes de repetir a reorganização.

O bootloader desta revisão emite a capacidade `LAYOUT_CAP_MAGIC`. Sem ela,
reorganização fica bloqueada. A tabela antiga é espelhada em 0x13000 e o registro
CRC de intenção fica em 0x14000, ambos na lacuna reservada antes da factory.
O registro só é armado após verificar a cópia. Se a tabela principal não coincide
com a antiga nem com a nova esperadas, o hook tenta restaurar a cópia verificada
antes de o SDK carregar a tabela. Após sucesso, invalida a intenção.

A origem e seu SHA ficam em NVS. Após reiniciar, SD reabre o plano do arquivo
retido; é preciso conferir/confirmar novamente, sem procurar o arquivo. Se mudou,
a execução é recusada. Cancelar o plano remove a retomada correspondente.

Testes simulam cópia corrompida, tabela parcial, falha de erase/write, cancelamento,
retenção da única cópia e preservação das outras regiões. **Isso não comprova
resistência física a falta de energia.** Bootloader/flash defeituosos ainda podem
exigir recuperação externa. Não há isolamento contra firmware externo que regrave infraestrutura.

## Atualização do aparelho já no layout deste projeto

Após backup completo atual e com autorização para flash, esta revisão precisa de
bootloader novo para o recurso de reorganização. A atualização de recursos preserva
**a tabela atual**, apps e dados:

```sh
python -m esptool --chip esp32p4 -p <PORT> read_flash 0 0x1000000 backup-atual.bin
python -m esptool --chip esp32p4 -p <PORT> write_flash \
  0x2000 build/bootloader/bootloader.bin \
  0x20000 build/tab5_launcher.bin
```

Compare o layout e os offsets com `build/flasher_args.json` antes de usar em outro
aparelho. Não use merged inicial, `ota_data_initial.bin` ou a tabela padrão como
atualização de um aparelho já reorganizado em 8 MB.

## Testes reproduzíveis

```sh
idf.py build
python tests/run_all.py
python tools/package_firmware.py
```

O runner cobre parser, pacotes/backups, instalador real com SDK simulado, arquivos,
registro dos apps, política/runtime de boot, layout/recuperação, RTC e LVGL real
(HOME, SD, Wi-Fi, formulários, configurações, assets, status e descanso), com
ASan/UBSan onde indicado nos testes. Renderizações são do firmware em host.

No aparelho ainda é necessário conferir: espaço livre contra o computador,
restauração real, pacote com dados, contagem/cancelamento, persistência do RTC
sem alimentação principal, carga/desligamento, uso prolongado de wallpaper e
teclado/touch para despertar. Ensaios de corte de energia exigem procedimento físico próprio.
