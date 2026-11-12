# Limites e pendências — 0.8.0-rc.2 (Release Candidate)

A01–A05 corrigidos no código e nos testes host: [CHANGES_0.7.3.md](CHANGES_0.7.3.md). Incluídos na 0.7.4 gravada; criação de pastas e leitura/escrita USB validadas fisicamente. Os demais cenários de recuperação exigem validação específica. A06–A13 da [auditoria](CODE_AUDIT_2026-11-12.md) continuam pendentes.

Implementação atual: [CHANGES_0.8.0-rc.2.md](CHANGES_0.8.0-rc.2.md), com instalação limpa autorizada. O gráfico distingue os dados reais da área sem partições. A fronteira atual do alocador permanece em 0xa10000, mesmo sem vfs/spiffs; a região posterior não é prometida como capacidade de instalação. Uso prolongado antes da versão final. Histórico: [CHANGES_0.7.4.md](CHANGES_0.7.4.md); histórico [CHANGES_0.7.1.md](CHANGES_0.7.1.md); base de armazenamento em [CHANGES_0.7.0.md](CHANGES_0.7.0.md).
Evidência da gravação e testes atuais: [BUILD_0.7.4.json](BUILD_0.7.4.json).

- Flash: mapa gráfico com reservas manuais e alocação automática por tamanho real, até 16 apps, preservando os existentes se todos couberem. O banco disponível depende do tamanho mínimo alinhado do launcher. Backup de
  16 MiB e restauração pela tela exigem bootloader/launcher idênticos e mesma
  infraestrutura fixa; outra revisão usa o utilitário USB validado. Edição
  arbitrária/migração de partições de dados ainda não existe. Ver CHANGES_0.5.0.md.
- Dados por app: perfis brutos de partições de aplicativos (FAT/SPIFFS/NVS existentes), captura no retorno,
  restauração antes do boot e recuperação/rollback implementados. Associação
  conhecida por pacote ou configuração explícita. Binário isolado não informa
  posse de dados. Snapshots antigos são retidos, sem limpeza automática.
- Formatos: app-only, merged de base 0 com um app, pacote schema 1 e backups
  próprios. Não aceita qualquer manifesto/binário de terceiros, imagens assinadas
  ou instalação com Secure Boot/flash criptografada. Dados restauráveis pela UI
  limitam-se a vfs/spiffs conhecidos. Apps maiores que o banco disponível não cabem.
- OTA: cache filtrado, retomada Range/ETag e consulta/download em lote por origem
  implementados. Sem validador seguro a origem exige reinício do arquivo. Versão
  diferente é candidata, não prova de versão mais nova. Sem atualização diferencial
  do índice; primeira consulta recebe o índice completo. SHA local sem hash da
  origem verifica transferência, não autoria. Apps antigos precisam de associação
  explícita da origem; não há detecção confiável só por nome.
- Web: pasta atual, árvore de diretórios, sessão e overlay implementados. Até
  16 MiB por arquivo, 32 níveis, 5000 itens na árvore arrastada. Nenhuma retomada
  por blocos após interromper um arquivo; reenvio recomeça esse arquivo.
  Edição/remoção/cópia e particionamento implementados. Operações longas têm
  análise/confirmação e consulta de progresso; atualização automática do índice OTA
  e retenção de backups continuam com os limites descritos acima.
- SD: montagem própria SDMMC com fallback SPI para retorno de apps SDSPI; nenhum formato automático. 16384 entradas/pasta; cópia até 16 níveis/100000 itens; editor 4095 bytes.
  Exclusão recursiva com prévia (16 níveis/100000 itens), sem lixeira. FAT/exFAT não fornecem transação resistente a todo corte
  de energia; links simbólicos não são suportados neste backend.
- Imagens: PNG/JPEG baseline até 4 MiB codificados, wallpaper 1280×720, ícone
  256×256; caminho do wallpaper até 95 bytes. Seletor filtra extensões, decodificador
  valida o conteúdo. Preparação de PNG na WebGUI com crop central/ajuste/giro e
  prévia. JPEG progressivo é convertido no navegador, não no decoder nativo. Sem
  edição livre ou miniaturas de todas as imagens no seletor. Capa OTA opcional até 512 KiB.
- Fontes: Latin Extended, marcas combinantes e pacotes SD CJK/emoji monocromático.
  Hangul, Unicode completo, emoji colorido/ZWJ e normalização completa pendentes.
  Pacotes verificados carregam em worker/memória; não há fontes arbitrárias pela UI. Nome OTA é limitado e caracteres inválidos para FAT
  são substituídos no nome do download.
- USB: console Serial/JTAG e modo exclusivo MSC pela USB-C. É preciso ejetar no
  computador antes de sair; não funciona simultaneamente com o launcher. A ligação USB-C é Full Speed, com buffer MSC de 2 KiB; entrada e saída continuam exigindo reinício controlado, com backlight oculto até renderizar a tela.
  Pendrives FAT/exFAT na USB-A são montados como D:, com leitura/gravação, instalação e ejeção. Não há hub/múltiplas unidades nem NTFS; a remoção física durante escrita pode perder dados. USB-A compartilha o host com o CDC opcional do Pi.
- Pi/Tab5Link: CDC/hora/shutdown, prateleira/backup e ASK remoto implementados,
  com testes simulados. Sem Pi disponível para validação física.
- Energia: aviso e CSV rotativo implementados; desligamento crítico opcional desativado por padrão. Bateria é estimada por tensão 2S; não há medidor de carga dedicado.
  Corrente em repouso não comprova USB/bateria cheia. Alimentação USB pode impedir
  corte de energia. Calibração configurável no SD, sem medições reais aplicadas. Faltam ensaios de
  autonomia/carga/desligamento e curva medida da bateria.
- Wi-Fi: conexão/retry e reconexão foram observados. Host ESP-Hosted 2.13.x e C6
  2.12.0 apresentavam aviso. Atualizador e imagem SDIO 2.13.1 usados, com nova versão e reconexão
  fisicamente confirmadas; ver BUILD_0.6.0.json. Faltam testes
  prolongados de falha de rede, hot-plug de SD/teclado e cortes de energia.
- Segurança de boot: firmware externo tem acesso à mesma flash e pode alterar
  launcher/tabela. Guarda registra falhas por slot, sem isolamento entre firmwares.
- Configuração ativa no SD. API legada de ponteiro ainda não oferece snapshot
  atômico a todos os leitores; estados operacionais passam a registros SD em `/launcher/private`. Não há inicialização/importação NVS pelo launcher. O mapa padrão não reserva NVS para aplicativos; criação/compatibilidade é responsabilidade do firmware externo. Snapshots lógicos NVS anteriores precisam de recaptura, não de restauração bruta.

A aprovação dos testes host não comprova teste físico de reparticionamento nem
resistência a cortes reais de energia. Na atualização 0.7.0, a tabela instalada foi preparada a partir do backup integral, sem alterar partições de dados. Reorganização durante instalação faz backup verificado e requer nova confirmação após reiniciar.
