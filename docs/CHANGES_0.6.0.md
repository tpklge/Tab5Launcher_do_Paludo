# 0.6.0 — WebGUI, USB disco e serviços

## Gerenciador web

HTML/CSS/JS continuam no microSD, em `/launcher/web`. Sessão por cookie HttpOnly
mantida. Criação de pastas, arrastar arquivos/pastas sobre a própria lista com
envio imediato e overlay por arquivo, lista compacta com tipo e tamanho, total de
bytes dos arquivos diretamente nesta pasta (não calcula o tamanho das subpastas).
Toda a pasta aparece em uma página; a resposta HTTP é enviada em partes e usa uma
única ordenação, até 16384 entradas. Navegação recente descarta respostas antigas.

Renomeação, cópia de arquivos/pastas, download, exclusão recursiva com confirmação
e editor de texto de até 4095 bytes. Edição exige conteúdo original igual e guarda
`.previous`. A API protege os diretórios de funcionamento e backups contra
renomeação/remoção e mantém a proteção das credenciais privadas. Não sobrescreve
arquivos de destino de cópias/uploads diferentes.

Flash e serviços: análise/instalação, remoção com backup do binário e opção de
backup dos dados, backup integral, restauração compatível, retomada e editor
visual dos slots de 8 MiB. Plano precede execução; um ticket de confirmação liga
a execução ao plano, e hashes são verificados pelo instalador. Operação em worker
com consulta de progresso mesmo após recarregar o navegador. Migração/restauração
reiniciam o aparelho. Limites de compatibilidade da versão 0.5.0 continuam válidos.

## 4. USB como armazenamento e exclusão recursiva

Modo exclusivo com TinyUSB 3.0.0: WebGUI / Flash e serviços / USB como disco, ou
`tab5 usb-disk confirm` pela serial. Reinicia, consome uma marca única em NVS,
monta/destaca FAT antes de entregar o cartão cru ao computador pela USB-C FS
(GPIO24/25). Launcher, Wi-Fi e perfis não iniciam nesse boot. Não há acesso FAT
simultâneo. Nunca formata o cartão. A tela indica o estado USB e oferece Voltar apos ejetar, com confirmacao.
Ejete no computador antes de sair ou pressionar RESET; o próximo
boot retorna ao launcher. Alternativa após ejetar: `tools/usb_disk_exit.py --ejected`
(PyUSB/libusb). A console Serial/JTAG desaparece enquanto o disco está ativo.

Exclusão recursiva no SD e web verifica primeiro toda a árvore: até 16 níveis e
100000 itens; raiz, links e arquivos especiais não são aceitos. Falha na prévia
não remove nada. Depois de iniciar a remoção, falha de mídia/energia pode deixar
remoção parcial; não é uma lixeira nem operação atômica.

## 5. Fontes e imagens

Pacotes opcionais no SD `/launcher/fonts/cjk.bin` e `emoji.bin`: Noto Sans SC e
Noto Emoji, 20 px, OFL. Lidos sequencialmente em worker, conferidos contra os hashes do pacote e
interpretados em memória como fallback da fonte latina, sem buscas no FAT na
tarefa gráfica. A interface inicia com a fonte latina enquanto o worker lê.
Cobrem os glifos presentes nos intervalos registrados em `fonts/manifest.json`:
Han, hiragana, katakana, pontuação e emoji monocromático. Não cobrem todo Unicode,
Hangul, todas as variantes regionais CJK nem sequências emoji ZWJ/cores. Fontes
faltantes preservam a interface latina. Reinicie depois de copiar os pacotes.
Gerador reproduzível `tools/build_unicode_fonts.py`, hashes e licenças no pacote.

WebGUI / Enviar / Preparar imagem: prévia local, wallpaper 1280×720 ou ícone
256×256, recorte central/preenchimento ou ajuste sem recortar e giro de 90°.
Converte os formatos decodificados pelo navegador (inclusive JPEG progressivo)
para PNG antes do envio; original preservado. Entrada até 16 MiB e 24 megapixels.
O decodificador nativo continua aceitando PNG e JPEG baseline com os limites
anteriores. Não é um editor livre de imagens nem acrescenta JPEG progressivo ao
firmware.

## 6. Raspberry Pi / Tab5Link

Cliente de prateleira `apps`, `backups`, `file` e envio de `backup`: recebimento
por blocos em arquivo temporário, flush/releitura antes de publicar; backup de
16 MiB primeiro verificado no SD, depois enviado ao Pi e relido por completo para
comparar SHA-256. WebGUI lista a prateleira/backups e recebe arquivos no SD;
instalação passa pela análise normal. Cópia local do backup é preservada.

ASK/ANSWER agora aceita solicitações limitadas/enfileiradas; `state`, `shot`
(RGB565 com cabeçalho), `boot` (padrão), `run`, `install` app-only e `backup`.
Operações longas entram no worker, com progresso em `state`; comandos inválidos
recebem erro. A instalação remota exige que o slot proposto pelo instalador seja
o solicitado; não substitui silenciosamente outro slot. Pi é um periférico local
confiável, habilitado explicitamente. Validação com Pi real continua pendente:
o equipamento não está disponível nesta sessão.

## 7. Energia, ensaios e C6

Calibração configurável em WebGUI / Flash e serviços: ganho da tensão (90–110%),
offset de tensão (±500 mV), zero de corrente (±100 mA), curva medida de 2 a 16
pontos monotônicos entre 6 e 9 V, de 0 a 100%. Salva em `/launcher/power.json`;
valores inválidos são recusados. A curva padrão continua se não houver curva
medida. Percentual segue estimativa por tensão, sem medidor de carga. Nenhuma
medição externa ou ciclo completo de carga foi inventado.

`tools/hardware_soak.py` coleta JSONL pela serial, com uptime, mínimo de heap,
Wi-Fi/motivo/RSSI, versão C6, tensão/corrente/estimativa e listagem SD. Duração
padrão 1 hora; registra falhas de comunicação e tenta reabrir a serial. Não remove
cartão, teclado nem alimentação; hot-plug e ciclos de energia exigem operador.

`tools/build_c6_firmware.py` compila isoladamente a fonte fixada ESP-Hosted 2.13.1,
SDIO/C6, com ESP-IDF 5.4.4. Saída `/launcher/c6`: binário, manifesto e configuração.
Console `tab5 c6-update SHA256 confirm` valida manifesto Tab5/SDIO, chip C6,
projeto/versão, limites, checksum e SHA antes de begin/write/end/activate. Usa
exclusão do instalador, pausa Wi-Fi e reinicia P4 após sucesso. Não grava
bootloader/tabela do C6. Revisão real é validada pelo bootloader remoto. Falha na
ativação pode ter estado indeterminado: reinicie e confira `status`.

Não afirmar atualização física, calibração ou ensaios prolongados sem consultar
as evidências em BUILD_0.6.0.json. Ensaios de autonomia/carga, medição externa,
hot-plug e cortes reais permanecem dependentes de hardware/operação.

Correções na validação física: fonte CJK não é interpretada diretamente do SD
(buscas repetidas bloqueavam a tela); retorno do USB restaura a seleção do PHY
Serial/JTAG antes de reiniciar e também na entrada normal de app_main.

Validação física também corrigiu estouro de pilha ao copiar pela WebGUI: buffers
e caminhos recursivos ficam no heap, a tarefa HTTP tem 16 KiB. O teste real de
30 arquivos, edição Unicode, cópia, renomeação e remoção recursiva foi repetido
com sucesso. As áreas OTA já estavam apagadas no backup anterior a 0.6.0;
a atualização não as restaurou nem alterou.

Referências de hardware/API: [Tab5/M5Stack](https://docs.m5stack.com/en/core/Tab5),
[USB Device/Espressif](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_device.html).
