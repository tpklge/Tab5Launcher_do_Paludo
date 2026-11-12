# 0.3.1 — menus, WebUI no SD, OTA e configuração

Pedido: código de acesso menor, texto maior, WebUI responsiva inspirada no
bmorcelli, HTML no microSD, correção OTA, CFG em três colunas, paleta em quadrados,
botão de desligar com símbolo e preferências em `/launcher`.

## Implementação

- Código web aleatório de **8 dígitos**, com zeros à esquerda, expiração de 15 min,
  comparação constante e bloqueio por 30 s após oito tentativas inválidas.
- Textos dos menus com mínimo de 20 px; títulos CFG 22 px. Status compacto e
  relógios preservam seus tamanhos próprios. Formulários também têm fonte maior.
- CFG: três colunas com rolagem independente: Aparência; Tela e relógio; Sistema
  e conexões. Dez quadrados de cor, foco visível, cor personalizada e botão de
  desligar fixo com símbolo no cabeçalho. A confirmação de desligamento permanece.
- WebUI própria com cartões, menu recolhível, tabela de arquivos, drag-and-drop,
  upload múltiplo sequencial, aplicativos, backup, iniciar/padrão, diagnóstico,
  captura de tela e configuração. Responsiva em 1280/768/390/320 px, sem CDN.
- Os arquivos reais ficam em **`/launcher/web/index.html`, `style.css`, `app.js`**
  no cartão. Não são embutidos no binário. Sem arquivos/cartão a rota retorna 503
  explicando o caminho. Pacote separado em `dist/tab5-microsd-web.zip`.
- Configuração ativa: `/launcher/config.json`; redes, fonte OTA, app padrão e
  metadados por identidade em `/launcher/private/*.dat`, com tamanho/magic/CRC,
  escrita temporária, flush, releitura e cópia anterior durante publicação.
  Wi-Fi usa cache validado em RAM para não acessar FAT a cada tecla.
- Primeira carga importa as preferências NVS antigas e `config.conf` se ainda não
  houver configuração nova. O arquivo antigo da raiz é retido em
  `/launcher/config.legacy.conf` após publicação bem-sucedida. NVS antiga é uma
  cópia de migração/rollback, não a autoridade das preferências quando o arquivo
  existe. Escritas futuras das preferências são no SD. Sem cartão, o launcher
  mantém recuperação local; não promete salvar configurações sem cartão.
- Journal de instalação, pedido de boot e guarda de falhas continuam internos:
  são estado de recuperação, precisam funcionar mesmo sem SD e não são temas,
  credenciais ou preferências editáveis. Partições de apps e dados não se movem.
- A WebUI bloqueia download dos arquivos privados também pelas variações de
  maiúsculas e nomes FAT com ponto/espaço final. Preferências públicas são
  acessíveis via API validada; senhas Wi-Fi não são retornadas pela API.

## OTA: causa e formatos reais

A serial da 0.3.0 registrou `HTTP_CLIENT: Out of buffer`. O cliente reservava 2 KiB
para cabeçalhos, insuficientes para alguns redirects do GitHub. A mensagem genérica
mencionava HTTP 200 mesmo em falhas de transporte. Agora usa 16 KiB, até cinco
redirects HTTPS e mensagens distintas para TLS/DNS, cabeçalhos incompletos, status
HTTP e formato inválido. Nunca desativa a validação de certificados.

Fontes aceitas:

1. API oficial: <https://m5burner-api.m5stack.com/api/firmware> — filtra categoria Tab5,
   última versão publicada. A API não traz chip em todas as entradas; a identificação
   ESP32-P4 é confirmada pelo parser do binário antes da instalação.
2. LauncherHub: <https://api.launcherhub.net/giveMeTheList> — filtra Tab5/P4. A URL
   <https://bmorcelli.github.io/Launcher/catalog.html> é convertida para esse feed,
   pois a URL original é HTML. Não usa proxy/HWID emprestado nem identificador do aparelho.
3. Manifestos `{version,url,notes}`, incluindo
   <https://github.com/0day1day/loadout-tab5/releases/download/v1.0.0/latest.json>.
   Se o manifesto pertence a uma release fixa, um URL `latest/download` do mesmo
   repositório é preso ao tag de origem, evitando outra versão silenciosamente.
4. O catálogo próprio schema 1 permanece aceito, com tamanhos e SHA-256 obrigatórios.

Feeds têm limite de 6 MiB e 128 entradas, paginadas de 24 em 24. Manifestos têm
limite de 64 KiB. Download é limitado a 32 MiB por componente. Quando a origem
não publica SHA-256, a UI informa **origem sem SHA**. O digest calculado é salvo em
`.observed-sha256`: registra apenas os bytes recebidos, não autentica o autor.
O instalador continua analisando chip, segmentos, checksum/hash e plano antes de
qualquer escrita; download não instala automaticamente. A consulta lê blocos de
4 KiB, tem limite total de dez minutos, progresso e cancelamento entre blocos;
não fica presa a uma única leitura do feed inteiro. Merged com dados/layout
não suportados continuam sendo recusados com preservação de apps existentes.

## Referências e testes

Referência visual, consultada em 11/11/2026: [bmorcelli WebUI](https://github.com/bmorcelli/Launcher/tree/2f43aa7bd3ef03e770c0a97135cbcfcc79c15aba/webUi),
[paleta CFG UNIZERSION](https://github.com/UNIZERSION/tab5-launcher/blob/da5aa9841f9c052578429fcc261cecaaae36ad1e/main/ui/l5_cfg.c).
HTML/CSS/JS implementados neste projeto, sem copiar dependências de gravação
absoluta, proxy, identidade HWID ou ferramentas NVS da referência.

- `tests/run_all.py`: regressões anteriores + adaptadores OTA, cabeçalhos, erros,
  redirects, arquivos SD com CRC, sessão/token/FAT e layout CFG.
- `tests/web_ui/run.py`: arquivos web reais em Chrome headless, API simulada;
  login, navegação, paleta, salvar, upload e ausência de overflow horizontal nas
  quatro larguras. Não equivale a ensaio HTTP completo no Tab5.
- Feeds reais baixados durante desenvolvimento: 85 entradas oficiais e 87 do
  LauncherHub passaram no adaptador. Testes host conferem versão publicada,
  filtro, URL insegura, manifesto sem hash e fixação de tag.
- Limites de pilha de callbacks e caches em PSRAM continuam verificados no ELF.

Instalação das páginas por console (com monitor fechado):

```sh
python tools/tab5_usb.py --port /dev/cu.usbmodem101 install-web sdcard/launcher/web
```

Somente os três arquivos web são publicados. Não sobrescreve configuração nem
arquivos privados. Também é possível copiar esses três arquivos diretamente para
`/launcher/web` no cartão. Evite substituir a pasta `/launcher` inteira.

## Verificações no aparelho

Backup completo antes do flash em `recovery/tab5-before-0.3.1.bin` (privado).
Gravação somente do launcher em 0x20000, com hash do esptool confirmado. Serial
confirmou 0.3.1 pronta, dois slots, tema preservado e conexão Wi-Fi com credenciais
salvas no cartão. `/launcher/config.json`, arquivo legado e pasta privada foram
observados no SD. Os três arquivos web foram enviados, publicados e relidos pelo
USB, com SHA-256 comparado aos originais do projeto.

A validação real também corrigiu o cliente USB: ordem RTS antes de DTR, seguindo
o monitor IDF, e envio em blocos de 64 bytes (o firmware aceita até 128). A ordem
anterior podia reiniciar o controlador USB; comandos longos excediam a recepção
real. Teste em `tests/tools/test_usb_client.py` cobre esses casos.

A consulta de feeds grandes no hardware expôs a janela TCP de recepção padrão
5760 bytes. Foi ajustada para 23040 (16 × MSS 1440) com caixa de recepção 18,
seguindo a relação recomendada pelo Kconfig do ESP-IDF. Buffers DMA/ESP-Hosted
continuam internos; não se altera firmware do C6. Logs de consulta reportam
progresso a cada 256 KiB e cancelamento ocorre entre blocos de 4 KiB.

Resultado final no Tab5: API oficial retornou 85 firmwares em 101,5 s nesta rede.
Antes da janela TCP maior, a transferência observada era ~8 KiB/s; após o ajuste,
~30–35 KiB/s. LauncherHub/bmorcelli retornou 87, Loadout retornou 1. Resultados e
limites de validação em [BUILD_0.3.1.json](BUILD_0.3.1.json). Novos menus físicos e
HTTP completo de ponta a ponta ainda dependem da interação do usuário; não inferir
esses testes a partir das capturas host ou dos testes de parser.
