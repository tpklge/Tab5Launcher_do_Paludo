# Formatos de instalação — 0.2.0

O SD usa **Analisar / restaurar**. Nenhuma escrita começa antes do plano e da
aceitação explícita. Firmware precisa ser ESP32-P4, chip 0x0012, com revisão,
segmentos, entrada, checksum e SHA válidos. Hash comprova integridade, não autoria.

## App-only `.bin`

Imagem completa de aplicativo, sem padding/assinatura adicional. Usa slot livre
antes de substituir outro. Requisitos de dados/drivers/offsets de um `.bin`
isolado não são inferidos; aceite essa limitação no plano.

## Merged `.bin`

Imagem baseada em **offset 0**, com tabela em 0x8000, checksum MD5, limites e
partições sem sobreposição. Deve conter exatamente um app válido. A imagem do
app é extraída pelos limites declarados, não por busca de magic. Mais de um app,
tabela inválida, dados parciais ou flags desconhecidas são recusados.

Bootloader, tabela, NVS, otadata e `aurora_cfg` da origem não são importados.
Dados FAT/SPIFFS só podem usar labels `vfs`/`spiffs`, caber nos destinos permitidos
e ter o mesmo offset do destino para merged. O plano lista esses dados e exige
backup integral antes de substituir. Outros layouts/labels são recusados.

## Pacote multipartes `.json`

Manifesto próprio, schema 1. Todos os componentes ficam na mesma pasta do JSON.
Não aceita URLs, caminhos relativos com diretórios, traversal, campos desconhecidos,
chaves duplicadas, labels protegidas ou hashes diferentes.

```json
{
  "schema": 1,
  "chip": "esp32p4",
  "app": {"file": "app.bin", "sha256": "<64 digitos hexadecimais>"},
  "data": [
    {"file": "filesystem.bin", "label": "spiffs", "sha256": "<64 digitos hexadecimais>"}
  ]
}
```

`data` é opcional; labels aceitas: `vfs` e `spiffs`, sem repetição. O serviço
valida tipo, offset protegido, tamanho, readonly/encryption e digest atual de cada
destino. Os dados não são convertidos: forneça uma imagem de filesystem compatível.

Geração do manifesto, com arquivos na mesma pasta:

```sh
python tools/make_package.py --app app.bin --data spiffs=filesystem.bin --output package.json
```

É um formato definido neste projeto, não compatibilidade genérica com qualquer
manifesto de outro launcher/M5Burner.

## Backups `.raw` + `.raw.json`

Schema `partition-backup`, tamanho/offset e SHA-256 íntegros. O seletor permite
restaurar slots de app conhecidos e VFS/SPIFFS permitidos. Fonte alterada é
recusada. Um backup novo do destino é obrigatório antes da restauração. App é
verificado pelo parser e pelo SDK; dados são verificados por hash após leitura.
Um `.raw` corrompido ou sem manifesto não é tratado como app-only.

Backup completo da flash é diferente: sua restauração é externa e repõe também
infraestrutura/configuração. Veja [recuperação](RECOVERY.md).

## Execução, cancelamento e layout

Origens e destinos são revalidados antes de apagar; inclusive slot incompleto ou
com dados recebe backup, pois apenas partição integralmente FF é considerada livre.
Escrita de app usa buffer de 4 KB, OTA e verificação do SDK. Dados têm backup e
hash de leitura posterior. App padrão substituído é desativado; instalação não
seleciona boot automaticamente.

Cancelamento antes do apagamento preserva o destino. Durante gravação de app pode
haver slot incompleto; o backup anterior permite recuperar. Depois de começar o
apagamento de dados/tabela, a etapa precisa terminar e cancelamento é desabilitado.
O journal impede autoboot após uma operação incompleta.

Imagens maiores que 4 MB podem usar o perfil controlado 2×4 → 1×8 MB, com backup
integral, bootloader com recuperação e retomada. Limites e atualização em
[seis etapas](SIX_STAGES.md). Não há esquema arbitrário, importação da tabela de
terceiros, imagens acima de 8 MB ou suporte a Secure Boot/criptografia nesse fluxo.
