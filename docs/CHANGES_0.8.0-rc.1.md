# 0.8.0-rc.1 — estado no SD e publicação como RC

**Release Candidate.** Uso e observação prolongados antes de considerar uma versão final; sem prazo definido. Readme principal em português, versão em inglês e referências de inspiração preservadas.

- Removidas as chamadas do launcher a `nvs_flash_init`, abertura de namespaces, leitura/gravação e importações legadas. Configuração, Wi-Fi, metadados, default app, guarda de boot, instalação e transações de perfil ficam em `/launcher` no SD.
- `launcher_state` publica cada transação operacional em um registro SD único com validação estrutural, limite de entradas/tamanhos, CRC do backend e publicação com recuperação. Alterações sem commit são descartadas. Escritores concorrentes são recusados; falhas não são tratadas como estado limpo. Os limites atuais são 24 chaves por registro e 600 bytes por valor.
- Wi-Fi iniciado com NVS desabilitado. O armazenamento do coprocessador C6 é independente: a imagem dele não foi reparticionada nem atualizada nesta etapa.
- Pedido de USB disco passa a usar marcador RTC com assinatura/inversa, consumido uma única vez e aceito somente em reinício por software. Não depende de FAT antes de iniciar o modo exclusivo. Reset físico ou falta de energia cancela o pedido.
- Mapa padrão para novas instalações sem partições `nvs` e `aurora_cfg`. Nenhuma entrada ou dado existente é apagado por atualização apenas do binário. Firmwares dependentes de uma partição NVS compartilhada devem criar/fornecer uma partição compatível; `nvs_flash_init()` não cria a tabela. Não há criação automática arbitrária de partições NVS pelo instalador.
- Perfis de dados copiam partições de aplicativos como bytes brutos, incluindo NVS existentes, sem interpretar/apagar namespaces. Snapshots lógicos antigos de NVS não são restauráveis como imagens brutas: validar/recapturar antes de usar o novo perfil. Não atualizar com operação de recuperação pendente. Não há migração de estados guardados apenas no NVS antigo.
- Driver USB MSC local mantém a correção da 0.7.4. Correções antigas de ESP LVGL Port/WiFi Remote/Hosted agora têm aplicação reproduzível e verificada por hash durante a configuração. Caminho local do driver no lock é ajustado quando o checkout muda de pasta.
- CI inclui `tools/check_no_nvs.py`, que inspeciona fontes, mapa padrão e símbolos do ELF. Um hook vazio de linkage do SDK é permitido; APIs NVS de persistência não são.

Build ESP-IDF 5.4.4, orçamento de memória e suíte host aprovados. Testes adicionais cobrem persistência, descarte sem commit, corrupção, publicação falha/retentativa, exclusão de escritores, limpeza de reservas e consumo do marcador RTC. [BUILD_0.8.0-rc.1.json](BUILD_0.8.0-rc.1.json).

**Sem flash nesta etapa.** O aparelho continua na 0.7.4, confirmada pelo usuário. O RC precisa de ensaio físico dos novos caminhos de recuperação, retorno entre aplicativos e USB disco, além de uso prolongado. Resultados físicos anteriores não certificam as alterações do RC. A06–A13 da auditoria continuam pendentes.
