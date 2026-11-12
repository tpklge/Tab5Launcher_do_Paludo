# 0.4.0 — transferências, catálogo e diálogos

- WebUI continua integralmente no microSD em `/launcher/web`, agora com quatro
  arquivos: index.html, style.css, app.js e transfers.js. Envio para a pasta
  aberta, criação recursiva de diretórios, seleção de pastas e arrastar árvores.
- Overlay por item, progresso, cancelamento, erros individuais e reenvio.
  Confirmação só após flush, fsync e releitura SHA-256 no cartão. Arquivo igual
  já recebido é sucesso idempotente; conteúdo diferente não é sobrescrito.
  Arquivos vazios são aceitos. Limite de 16 MiB por arquivo e 32 níveis.
- Login troca o código por sessão aleatória HttpOnly/SameSite=Strict, limitada
  a 8 horas e 2 horas de inatividade. Não guarda código em JS/localStorage.
  Recarregar restaura sessão; logout/encerrar servidor a revoga. Atualização
  da listagem após envio, inclusive página que contém o item recebido.
- Cache derivado do catálogo filtrado em `/launcher/cache`, com hash, validação
  de estrutura e identificação da fonte. Abertura usa cache; Atualizar origem
  força rede; falha da origem pode reutilizar o cache. Primeira consulta ainda
  precisa receber o índice completo publicado pela origem.
- Detalhes OTA: versão, descrição e tamanho quando disponíveis; consulta dos
  cabeçalhos para tamanho desconhecido; imagem HTTPS opcional até 512 KiB.
  Baixar e instalar leva à análise/consentimento existente do SD. Apenas baixar
  mantém os arquivos no SD. Binário único recebe nome do app e pasta exclusiva.
- Fontes com fallback Latin Extended/acentos e marcas combinantes, licença SIL
  OFL da Montserrat incluída. Isso não fornece todos os alfabetos/emoji.
- CFG compacta (18 px), entrada de data DD/MM/AAAA e hora HH:MM:SS separadas,
  com máscara e validação de calendário. Desligar usa Desligar/Cancelar.
- Seletores de wallpaper/ícone mostram pastas e PNG/JPEG, com leitura em worker,
  paginação e remoção da imagem. Limites de decodificação anteriores continuam.
- Campo de renomear 24 px, cabeçalho SD C:, ações com ícones e rótulos curtos.
- Remoção oferece backups extras das partições de dados. Associação por identidade
  é registrada quando um pacote declara dados. Apps isolados/anteriores não
  permitem provar posse de NVS/SPIFFS: aparecem como possivelmente compartilhados.
  Os dados selecionados são copiados/verificados, nunca apagados na desinstalação.
- Editor visual do banco de apps de 8 MiB: 1–4 slots, mínimo 1 MiB, passo 256 KiB,
  último slot ajustado automaticamente. Sistema/dados externos ficam protegidos.
  Aplicar requer confirmação, backup integral verificado e reinício. Remove todos
  os apps do banco e exige reinstalação/restauração manual. Layout inalterado
  é recusado. Transação espelhada mantém recuperação da tabela após interrupção.

- Correção de RAM antes do scheduler: buffer de diagnóstico de 8 KiB movido
  para PSRAM; verificador agora limita também o início global do heap interno.

## Validação e limites

Testes host com ASan/UBSan: parsers, instalação/backup, layouts e recuperação,
segurança da sessão/caminhos, LVGL real/teclado/diálogos. Navegador real em quatro
larguras, sessão restaurada, subpastas, arquivos vazios e falha/reenvio simulados.
Não aplicar reparticionamento no aparelho de uso para testar automaticamente:
seus aplicativos serão preservados durante a atualização do launcher.

## Restante

- Migração de apps preservando instalação ao redimensionar slots; edição/criação
  arbitrária de partições de dados e restauração automática de layout integral.
- Associação de dados para apps antigos/sem manifesto depende de metadados do
  autor; isolamento/alternância automática de NVS e dados entre apps pendente.
- Retomada de downloads OTA interrompidos e atualização diferencial/condicional
  do índice. Cache acelera reaberturas, não a primeira leitura completa.
- Fallback para CJK/emoji e outros alfabetos; normalização Unicode completa.
- Ensaio físico prolongado de Wi-Fi/SD/teclado e teste do Pi/Tab5Link quando houver
  Raspberry Pi disponível; atualização coordenada do firmware ESP-Hosted no C6.

## Resultado físico

0.4.0 gravada somente em 0x20000, hash confirmado pelo esptool. Backup de 16 MiB
em recovery/tab5-before-0.4.0.bin. Serial: launcher pronto, dois slots e reconexão
Wi-Fi. Quatro assets relidos pela USB com SHA-256 igual ao local. Consulta real
M5Burner: 85 firmwares, 86,639 s na primeira consulta e 0,359 s com cache no SD.

HTTP real pelo computador ficou sem acesso: computador em 192.168.123.x e Tab5
em 192.168.137.x, conexão expirada. Portanto envio HTTP no dispositivo e os novos
fluxos de toque ainda precisam de validação na mesma rede/interação física.
O editor de partições não foi aplicado e nenhum app foi desinstalado no aparelho.
