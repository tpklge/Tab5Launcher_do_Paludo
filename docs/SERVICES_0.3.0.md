# Serviços e manutenção — 0.3.0

Esta revisão acrescenta serviços funcionais aos módulos que eram stubs. O build e
os testes host não equivalem à validação física. O firmware 0.3.0 foi gravado somente em 0x20000, hash verificado, após backup
completo de 16 MiB. A serial confirmou versão, launcher pronto, dois slots, SD e
reconexão Wi-Fi. Os fluxos novos de UI e integração Pi ainda não foram testados fisicamente.

## Uso no aparelho

- **OTA:** configurar URL HTTPS de catálogo próprio schema 1, atualizar e baixar
  uma entrada. Até 32 entradas e cinco arquivos por entrada (manifesto, app e três
  dados). Todos os arquivos são baixados e verificados por tamanho e SHA-256.
  A primeira parte deve ser o ponto de entrada: app `.bin` ou manifesto de instalação
  `.json`. Depois, abrir a pasta informada no SD e confirmar o plano de instalação.
- **WEB:** conectar Wi-Fi e iniciar servidor. Abrir o endereço mostrado e digitar
  o token temporário no navegador. Ele expira após 15 minutos; encerrar/reabrir o
  serviço gera outro. O token não fica em URL, logs nem NVS. O transporte é HTTP,
  destinado a LAN confiável; token não criptografa a conexão. O header próprio,
  ausência de CORS e validação do token impedem escrita por formulários externos.
  Oito tentativas inválidas bloqueiam autenticação por 30 segundos.
- **FLASH:** inspecionar tabela e fazer backup integral de slot OTA. Backup usa a
  mesma trava, gravação e releitura SHA-256 do instalador. Restaurar selecionando
  `.raw` no SD. Não oferece edição arbitrária de bootloader/tabela/NVS.
- **DIAG:** testar teclas/modificadores, redetectar teclado, tentar conexão,
  reiniciar driver/C6, exportar snapshot/log e capturar BMP da tela no SD.
- **CFG:** exportar/importar configuração JSON completa sem credenciais; importar
  tema de dados schema 1. Importação de tema aceita apenas aparência e relógio,
  sem alterar rede, fuso ou particionamento. Campos, tipos, tamanhos, duplicatas,
  escapes e profundidade são validados antes de alterar configuração.
- **PI:** opcional e desligado por padrão. Iniciar USB-A host, consultar HELLO/STAT,
  sincronizar hora e solicitar SHUTDOWN via protocolo Tab5Link. Exige Pi separado
  com programa compatível. `SHUTDOWN enviado` não prova halt; não corta 5 V.
  Sem Pi disponível, esta revisão só tem testes de protocolo/compilação. ASK remoto,
  atualização do programa Pi, prateleira/download/backup via Pi ainda não integrados.
  Um Pi ainda conectado bloqueia boot de outro firmware e power-off pela CFG.

## Catálogo próprio

Gerar o índice com arquivos reais, sem hashes fictícios:

```sh
python tools/make_catalog.py --entry pasta/package.json \
  --base-url https://seu-servidor.exemplo/firmware/ \
  --id meu-app --name "Meu app" --version 1.0 --output catalog.json
```

O usuário publica o índice e os arquivos no servidor escolhido. A ferramenta não
publica automaticamente. Também aceita `--entry app.bin`. URLs HTTPS, bundle de
CAs do ESP-IDF, até cinco redirects HTTPS e relógio válido são obrigatórios.
Downgrade para HTTP é recusado. Limites: índice 64 KiB; arquivo 32 MiB; buffers de
4 KiB. Pastas de download têm nomes aleatórios e não sobrescrevem apps/arquivos.
Cancelamento/falha remove o diretório incompleto. Download interrompido recomeça;
Range/ETag, cache persistente, favoritos e adaptadores M5Burner/LauncherHub/GitHub
Releases ainda não foram implementados. Não se anuncia compatibilidade genérica
com catálogos externos nem autoria criptográfica a partir de SHA-256.

## API web desta revisão

Toda rota `/api/*` exige `X-Launcher-Token`. Sem credenciais Wi-Fi nas respostas.

| Método | Rota | Resultado |
|---|---|---|
| GET | `/api/status` | versão, reset, memória, rede, bateria estimada |
| GET | `/api/apps` | slots e descritores; não é health-check de firmware |
| GET | `/api/files?path=...&page=0` | listagem paginada de 24 itens |
| GET | `/api/file?path=...` | arquivo do SD até 32 MiB |
| POST | `/api/upload?name=...` | corpo bruto, 1 byte a 16 MiB, pasta nova `incoming` |
| POST | `/api/screenshot` | grava BMP no SD e retorna caminho para download |
| POST | `/api/log` | grava snapshot/log no SD e retorna caminho para download |

A página usa `textContent`, não injeta nomes de arquivos como HTML. Caminhos são
validados pelo mesmo núcleo do SD, com decode único, rejeição de NUL/controles e
traversal. Upload usa `.part`, flush/fsync e publicação final apenas ao concluir;
limite de sessão de envio 180 segundos e timeout de socket de cinco segundos.
O servidor atende um handler por vez e no máximo três sockets. Não há exportação
MSC compartilhando o cartão. FAT não garante publicação resistente a queda de energia.
Nesta revisão a instalação/análise destrutiva e a administração de apps continuam
na tela física, evitando uma segunda política de flash independente.

## Console USB Serial/JTAG

Fechar `idf_monitor` antes de abrir outro cliente na mesma porta. Python 3.9+ e
`pyserial` no computador (ou Pi em modo host conectado ao USB-C de manutenção):

```sh
python tools/tab5_usb.py --port /dev/cu.usbmodem101 status
python tools/tab5_usb.py --port /dev/cu.usbmodem101 ls /sdcard
python tools/tab5_usb.py --port /dev/cu.usbmodem101 upload app.bin
python tools/tab5_usb.py --port /dev/cu.usbmodem101 backup 0
python tools/tab5_usb.py --port /dev/cu.usbmodem101 screenshot
python tools/tab5_usb.py --port /dev/cu.usbmodem101 download /sdcard/arquivo.bin arquivo.bin
```

Console também aceita `tab5 boot 0`, `tab5 wifi-retry` e `tab5 abort`. `boot` faz
verificação de imagem e usa retorno one-shot; não instala nem edita a tabela.
Transferência usa begin/tamanho/SHA-256, chunks hex de 128 bytes com offset exato,
commit após flush e hash. Não sobrescreve arquivos existentes. Interrupção antes
de commit pode deixar `.part`; `abort` remove o temporário da sessão corrente.
USB Serial/JTAG não implementa MSC. USB-A host/Pi e USB-C manutenção têm funções
separadas; não houve teste nesta revisão em Windows/Linux nem de MSC/roteamento OTG.

## Verificação e limites restantes

`python tests/run_all.py` inclui parsers, segurança web (token, expiração, bloqueio,
decode), transferência USB com SHA/offset/abort, framing Pi, UI serviços com 100 ciclos, e todas as regressões anteriores.
Build mede frames da UI; caches grandes continuam na PSRAM; snapshots limitam
1280×720. Otimização `-Os` abre margem no slot do launcher sem alterar a tabela.
CI `.github/workflows/firmware.yml` compila ESP-IDF 5.4.4, roda testes e empacota
artefatos distintos. Workflow foi criado; ainda não executado no GitHub.

Ainda faltam a paridade completa da WebUI (instalar/editar/remover/NVS remotamente),
adaptadores/cache/resume do catálogo, MSC comprovado, serviços completos Pi,
perfis avançados de partições/migração/restore completo, seleção de imagens por
miniaturas/cores extraídas, logs persistentes rotativos/coredumps e ensaios físicos
prolongados/cortes de energia. Não marcar esses requisitos como concluídos.
