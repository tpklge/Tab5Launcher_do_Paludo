# 0.4.2 — SD após firmware que usa SPI

Inclui as correções de lançamento de [0.4.1](CHANGES_0.4.1.md).
Durante o teste real, tab5_meshtastic_v2 iniciou normalmente, mas o retorno ao
launcher deixou a inicialização SDMMC em timeout. O app instalado usa SDSPI/SPI3;
um reset do P4 não garante reinicialização elétrica do cartão em modo nativo.

O launcher agora controla seus próprios handles de cartão, alimentação e barramento:

- SDMMC slot 0, 4 bits, 20 MHz como primeira tentativa.
- Se falhar, tenta SDSPI no SPI3, 10 MHz, com os pinos do BSP do Tab5.
- A limpeza da tentativa nativa preserva o slot 1 do ESP-Hosted.
- Alimentação LDO 4 é mantida durante a troca de tentativa, evitando handles
  duplicados do BSP; barramento/alimentação são liberados se ambas falharem.
- Montagem serializada; pode ser tentada novamente. Nenhum caminho formata o
  cartão. Capacidade e espaço livre usam o handle do transporte que funcionou.

Teste host ASan/UBSan usa a implementação real de board_sd.c para verificar
montagem nativa, fallback SPI, ausência de formatação, falhas, limpeza e repetição.
Validação física de inicialização, leitura e ida/volta registrada em BUILD_0.4.2.json.

O teste inicial na 0.4.2 confirmou SD acessível e listagem de /launcher; novo lançamento do slot 0 confirmou pausa ESP_OK e inicialização do Meshtastic.

Retorno físico confirmado: SDMMC retornou ESP_ERR_TIMEOUT, fallback recuperou o cartão em SPI/10 MHz, launcher 0.4.2 ficou pronto. Console confirmou sd=true e listou /sdcard/launcher novamente.
