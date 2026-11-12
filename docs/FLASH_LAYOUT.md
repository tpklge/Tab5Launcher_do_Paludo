# Layout de flash — Tab5 16 MB

Fonte: `partitions.csv`, validada pelo build. Endereços de gravação de firmware
estão em `build/flasher_args.json` e no pacote gerado por `tools/package_firmware.py`.

| Região | Endereço | Tamanho | Uso |
|---|---:|---:|---|
| Bootloader | 0x2000 | até 0x6000 | Loader IDF + hook de boot único |
| Tabela | 0x8000 | 0x1000 | Partições + checksum MD5 |
| nvs | 0x9000 | 0x6000 | Configuração, Wi-Fi e journal |
| phy_init | 0xF000 | 0x1000 | Calibração |
| otadata | 0x10000 | 0x2000 | Metadados OTA do SDK |
| launch_req | 0x12000 | 0x1000 | Pedido de boot único + capacidade do loader |
| launcher | 0x20000 | 0x1F0000 | App factory |
| app0 | 0x210000 | 0x400000 | Primeiro app OTA, 4 MB |
| app1 | 0x610000 | 0x400000 | Segundo app OTA, 4 MB |
| vfs | 0xA10000 | 0x500000 | Dados FAT existentes |
| aurora_cfg | 0xF10000 | 0x6000 | Preservada |
| spiffs | 0xF16000 | 0xDA000 | Dados existentes |
| coredump | 0xFF0000 | 0x10000 | Diagnóstico |

A revisão anterior tinha um slot OTA de 8 MB. A migração atual preserva o app
existente somente após conferir imagem/checksum/SHA e metade superior apagada,
com backup completo de 16 MB. `tools/prepare_multiboot_upgrade.py` recusa outras
alterações de tabela ou qualquer dado na futura região app1. Apps maiores que
4 MB são recusados pelo instalador. Reparticionamento genérico continua pendente.

## Política de retorno

O hook `bootloader_components/launcher_policy/hooks.c` carrega a tabela validada
pelo SDK e seleciona factory por padrão. Um pedido com magic, slot, endereço,
tamanho e CRC32 válidos pode selecionar um app OTA uma vez. O pedido é invalidado
antes de ser apagado e antes do salto; no próximo RESET, crash, power cycle ou
wake de deep sleep, não há pedido e o loader escolhe o launcher.

O SDK ainda verifica/carrega a imagem. Não há transferência de infraestrutura de
um firmware externo. Apps que regravem a flash ou a tabela podem destruir esse
caminho; não existe isolamento entre apps. Política distinta para deep sleep,
autoboot e detecção histórica de crashes ainda precisa ser concluída.

## Recuperação da reorganização — 0.2.0

Sem alterar a tabela padrão, reserva lacunas 0x13000/0x1000 para espelho da tabela
anterior e 0x14000/0x1000 para intenção CRC. Nenhuma partição do layout reconhecido
pode sobrepor essas regiões. Novo marcador de capacidade em `launch_req + 68`
impede a rotina de reorganização de operar com um loader sem recuperação.

Perfil automático 2×4 → 1×8 MB: app0 mantém 0x210000 e cresce para 0x800000;
app1 é removido. Outras entradas permanecem idênticas; checksum MD5 é regenerado.
Backup integral do SD, NVS de retomada e espelho precedem a tabela principal.
Esse perfil não é compactação preservando dois apps e não muda VFS/aurora/SPIFFS.
Ver [seis etapas](SIX_STAGES.md) e [recuperação](RECOVERY.md).

## Guarda de operações 0.5.0

0x15000–0x15FFF fica reservado para a guarda persistente de migração/restauração.
Não é partição de dados; operações recusam tabelas que ocupem esse intervalo.
O registro detalhado e o backup ficam no SD. As guardas de recuperação da tabela
em 0x13000/0x14000 permanecem; não há alteração da tabela na atualização do launcher.

Setores não particionados podem conter resíduos de imagens antigas. Eles são
incluídos no backup integral; a nova guarda só é escrita após esse backup ser
verificado. A presença de resíduos não impede criar/verificar backups.
