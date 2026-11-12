# Ampliações futuras — etapa pausada

Registro de 12/11/2026, a pedido do usuário. A implementação das ampliações foi
pausada e revertida. Os dois arquivos iniciais de retomada de upload foram
removidos; não houve integração, compilação ou gravação dessas ampliações.
Nenhuma unidade foi formatada e nenhum arquivo dos cartões foi alterado nesta etapa.

A versão instalada permanece 0.7.1. A correção de despertar 0.7.2 foi preparada
na etapa anterior e continua sem gravação; não faz parte desta reversão.

## 1. Retomada de uploads interrompidos

- Enviar em blocos, consultar o deslocamento persistido e retomar após reconectar.
- Verificar cada bloco e o arquivo completo com SHA-256.
- Manter temporário e metadados no mesmo volume do destino; publicar apenas
  depois da verificação, preservando a versão anterior até concluir.
- Tratar reenvio de bloco e confirmação perdida sem duplicar bytes.
- Recuperar interrupções durante a publicação, descartar sessões explicitamente
  e definir limpeza de temporários antigos.
- Adaptar overlay, cancelamento e reenvio na WebGUI. Persistir identificação dos
  envios sem salvar credenciais; após recarregar, pedir nova seleção dos arquivos
  quando o navegador não puder conservar os respectivos acessos.
- Testar queda da rede, reinício, offset incorreto, arquivo diferente com o mesmo
  nome, hash inválido, falta de espaço e remoção da unidade.

## 2. Hubs e múltiplas unidades USB

- Habilitar suporte a hubs no SDK e respeitar as limitações do controlador host
  e de dispositivos Full/Low Speed atrás de hubs nesta versão do SDK.
- Manter C: para o SD; planejar D:, E: e F: para até três unidades USB.
- Controlar disponibilidade, referências de operações, capacidade e ejeção por
  unidade. Desconectar uma unidade não deve invalidar as demais.
- Atualizar caminhos, navegação nativa, WebGUI, instalador, cópias entre unidades
  e política de energia. Ajustar o número de volumes FAT.
- Validar com hub e várias unidades reais, incluindo desconexão durante leitura,
  fila cheia e ejeção enquanto houver uma operação ativa. Nenhuma formatação automática.

## 3. NTFS

- Avaliar uma implementação compatível com ESP32-P4, limites de memória e licença.
- Primeira fase proposta: somente leitura, com listagem, cópia para outro volume
  e leitura de firmware para instalação. Escrita NTFS exige etapa própria.
- Cobrir identificação do volume, MBR/GPT, UTF-16, arquivos residentes e não
  residentes, limites e validação das estruturas; recusar recursos não suportados
  explicitamente, sem apresentar dados incorretos.
- Testar imagens NTFS reais, fragmentação, arquivos grandes e estruturas corrompidas
  antes de conectar ao VFS e validar em uma unidade física apropriada.

## 4. Unicode e emoji

- Ampliar os pacotes de fontes no SD, começando por Hangul e alfabetos ausentes.
- Avaliar normalização Unicode e cobertura real, sem prometer todos os caracteres
  apenas por aceitar UTF-8.
- Avaliar emoji colorido e sequências compostas/ZWJ no LVGL 9.3, com atlas ou
  pacotes no SD, memória limitada e licenças/atribuições preservadas.
- Não fazer leituras lentas do cartão no callback de renderização da fonte.
- Preservar fallback legível quando um pacote estiver ausente ou um glifo não existir.

## 5. Prévias de imagens

- Adicionar miniaturas/prévia aos seletores de wallpaper e ícone e ao gerenciador.
- Usar worker, cache limitado e descarte de resultados de telas já fechadas.
- Ampliar formatos apenas quando houver decodificador adequado; definir limites
  de bytes, dimensões e memória e recusar imagens malformadas.
- Preservar os originais e testar navegação rápida, acentos nos nomes, falta de
  memória, imagens grandes e remoção do volume durante a leitura.

## Antes de retomar

Conferir o estado do código e do aparelho. Validar a correção de despertar já
preparada separadamente destas ampliações. Se o firmware crescer além da reserva
atual, preparar a atualização a partir de backup integral recente, preservando
apps e partições de dados; não gravar a tabela padrão da compilação sobre o aparelho.

Referências consultadas nesta avaliação:

- [USB Host — Espressif](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)
- Headers e Kconfig locais do ESP-IDF 5.4.4, USB Host MSC 1.3.0 e LVGL 9.3.0.
