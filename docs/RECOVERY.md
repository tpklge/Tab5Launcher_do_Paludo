# Recuperação — 0.2.0

## Retorno e guarda

Com o bootloader do projeto, um pedido CRC é consumido antes do boot do app.
RESET/retorno do app volta à factory. NVS registra o início e o motivo do retorno;
três falhas consecutivas de panic/watchdog bloqueiam o autoboot daquele slot.
Retorno de um app suprime autoboot naquele boot, mesmo sem atingir o limite.
A HOME permite reativar o padrão explicitamente. Brownout/power-on não são
atribuídos a uma falha de software do app.

Não há isolamento contra um app externo que regrave bootloader/tabela/flash.

## Instalação ou restauração interrompida

Launcher, bootloader, NVS e `aurora_cfg` não são destinos comuns do instalador.
App e os dados explicitamente listados no plano podem ser substituídos, sempre
com backup verificado antes. O journal incompleto bloqueia autoboot. Reinstale
um arquivo válido ou selecione o `.raw` com `.raw.json` íntegro para restaurar.

A restauração de dados é restrita a VFS/SPIFFS conhecidos. Backup completo de
flash não é um backup de partição instalável pela UI.

## Reorganização 2×4 → 1×8 MB interrompida

Exige o bootloader com capacidade nova. Antes de modificar a tabela, preserva
16 MB no SD, relê/hash verifica e arma a recuperação em lacunas reservadas:

| Região | Uso |
|---|---|
| 0x13000–0x14000 | Cópia anterior da tabela, 4096 bytes |
| 0x14000–0x15000 | Intenção CRC com hashes CRC da tabela antiga/nova |

No boot, o hook verifica a intenção. Se a tabela principal não corresponde à
antiga nem à nova esperadas, restaura somente uma cópia cujo CRC confere e só
então invalida a intenção. Se já corresponde à nova íntegra, mantém o novo layout.
O SDK ainda valida a tabela/imagem. Falha ao recuperar não autoriza carregar uma
tabela inválida; o caminho de recuperação externo continua necessário.

Após reiniciar normalmente, SD reabre o plano da origem salva em NVS. Confirme
novamente. Origem alterada é recusada; cancelar remove essa retomada. A única
cópia `layout-before.bin` não é sobrescrita em uma nova tentativa.

Esses caminhos têm testes de falha simulada, não ensaios físicos de corte de energia.

## Edição de texto interrompida

O editor sincroniza um temporário e renomeia o original para `.previous` antes de
publicar. FAT não oferece rename sobre destino existente. Se a energia falhar
entre as renomeações, recupere o original pelo `.previous`; `.edit-part` pode
conter a edição ainda não publicada. Não há promessa de transação de energia.

## Recuperação externa e atualização

Backups locais em `recovery/` contêm configuração privada. Use uma cópia íntegra,
com data, tamanho e SHA conferidos. Restaurar 16 MB repõe também bootloader,
tabela e configurações da data do backup:

```sh
python -m esptool --chip esp32p4 -p <PORT> write_flash 0x0 <BACKUP-COMPLETO.bin>
```

Para atualizar recursos desta revisão preservando o layout atual do projeto:

```sh
python -m esptool --chip esp32p4 -p <PORT> write_flash \
  0x2000 build/bootloader/bootloader.bin \
  0x20000 build/tab5_launcher.bin
```

Faça backup atual antes. Não grave a tabela padrão sobre um aparelho já no perfil
8 MB. Não use merged inicial nem `ota_data_initial.bin` como atualização.
O firmware não apaga NVS automaticamente quando a inicialização falha.
