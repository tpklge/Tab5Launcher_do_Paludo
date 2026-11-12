# USB-A: criação de pastas sem ponteiro inválido — 0.7.4

A serial da 0.7.1 registrou falha de alocação DMA de 32768 bytes, com maior bloco disponível de 28672 bytes, seguida de `tlsf_free: block already marked as free`. A operação era WRITE10 durante criação de pasta exFAT. O driver liberava a transferência antiga antes de tentar alocar a maior; falhando, `device->xfer` continuava apontando para a memória liberada e a consulta SCSI de erro tentava usá-la novamente.

A cópia local de usb_host_msc 1.3.0 mantém a transferência anterior até alocar a substituta com sucesso. Leituras/escritas FatFS são divididas em comandos de até 4096 bytes, alinhados a setores, evitando alocações proporcionais ao cluster exFAT. Primeiro erro interrompe o restante da operação. A dependência tem `override_path` explícito no manifesto para que a correção sobreviva à reinstalação dos componentes gerenciados; a compilação deve usar `components/espressif__usb_host_msc`.

Teste host executa as funções reais de transferência e diskio com falhas injetadas: alocação, liberação, reutilização para tratamento do erro, clusters de 64 KiB, último bloco parcial, setores de 512/1024/2048/4096 bytes e interrupção após erro. ASan/UBSan e suíte completa aprovados. A versão inclui as correções preparadas em 0.7.2 e 0.7.3. Evidências finais de build, gravação e teste físico em BUILD_0.7.4.json.

A correção não elimina perdas por remoção física durante escrita ou por corte de energia. Não há formatação automática nem alteração de arquivos do usuário para recuperar o volume.

Firmware gravado em 0x20000 com hash verificado, após backup integral de 16 MiB e leitura do mapa instalado. Tabela, aplicativos e partições de dados não foram regravados. Teste físico passou: 13 pastas no D:, escrita/leitura de 256 KiB com SHA-256 idêntico, listagem e exclusão recursiva apenas da pasta de validação. D: permaneceu montado e o uptime aumentou sem reinício. Não foi realizado ensaio prolongado.
