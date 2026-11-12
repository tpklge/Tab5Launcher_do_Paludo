# RC preparado — 0.8.0-rc.1

[Remoção de NVS e publicação bilíngue](CHANGES_0.8.0-rc.1.md). Build, ausência de rotinas NVS e regressões aprovados; sem flash. Validar fisicamente: salvar configurações/Wi-Fi e reiniciar; iniciar/retornar de apps; modo USB uma única vez, cancelamento por reset físico; recuperação de operação interrompida; falha de SD sem escrita em flash NVS; perfis brutos e recaptura de snapshots legados. Uso prolongado antes da versão final.

# Revisão instalada — 0.7.4

[USB MSC corrigido](CHANGES_0.7.4.md), build e suíte host aprovados, flash apenas do app com hash verificado. Teste físico no D: criou 13 pastas, verificou 256 KiB por SHA-256 e removeu os dados de teste sem reinício. [Registro](BUILD_0.7.4.json). Ainda faltam ensaios prolongados e os cenários específicos de recuperação descritos abaixo.

# Revisão preparada — 0.7.3

Cinco correções críticas e regressões concluídas, sem flash: [detalhes](CHANGES_0.7.3.md) e [evidências](BUILD_0.7.3.json). No aparelho, validar recuperação de configuração, edição com arquivo auxiliar existente, recusa de boot com instalação pendente e transições USB/boot/desligamento durante upload, sem remover o cartão durante escrita. Cortes de energia exigem ambiente de recuperação e backup; não foram ensaiados.

# Validação atual — 0.7.1

Casos adicionais: todos os nove temas na seleção nativa e persistência JSON dos cinco novos; data por extenso em português, meses, dias com acento, buffers pequenos e data inválida. Ciclos dos quatro relógios preservados, com dim/off, wake e bloqueio durante operação crítica.

Compilação verifica a margem de DRAM e a pilha. No hardware, verificar ida/volta USB após ejeção, leitura/escrita com hash e ausência visual do clarão durante o reinício. A interface deve reaparecer com as configurações originais após os ensaios. O aumento do buffer não implica aumento proporcional da velocidade de transferência; a USB-C permanece Full Speed.

# Validação da base — 0.7.0

`python3 tests/run_all.py` executa as regressões atuais. A compilação e o pacote
verificam os orçamentos de DRAM antes do scheduler e da pilha da UI.

Casos novos: paleta 144 cores/grade 12×12, fontes OTA manuais com cancelamento
e falha parcial, reserva mínima do launcher, apps deslocados preservando bytes
anexados, plano alterado rejeitado, proteção do SDK restaurada após falha de
gravação da tabela, backup opcional e staging de backup bruto com sidecar.

Novos casos também cobrem editor gráfico nativo/Web, reservas manuais, mínimos por app, estouro de capacidade, propriedade das solicitações SD por ponteiros e falhas de fila/alocação. Foram feitas 25 trocas reais entre SD/FLASH/CFG/WEB/HOME após a otimização de RAM, além da aplicação física do mapa manual.

No Tab5 foram exercitados dois apps e alocação automática, inicialização do
Microcity e retorno, remoção sem backup, C:/D: com upload/substituição/mesclagem,
e comparação dos 6.225.920 bytes protegidos depois das transações. Navegador
real em 1280/768/390/320 px; ejeção e persistência do arquivo USB após remontar.
Resultados completos em BUILD_0.7.0.json. Não equivale a corte de energia,
ensaio de autonomia ou validação Pi.

Os registros abaixo são históricos e não descrevem os limites atuais.

---

# Regressão de pilha SD — 0.2.1

`python tools/check_ui_stack.py` lê os frames `.su` emitidos pelo compilador.
Callbacks não podem ultrapassar 3 KB; `submit` do SD fica limitado a 512 bytes
(atual: 32). Workers usam suas pilhas próprias e ficam fora desse limite.
O empacotamento também executa essa verificação.

`test_sd` injeta falha de alocação do pedido, verifica busy/fila e repete 100 ciclos.
Teste físico opcional de navegação (somente leitura):

```sh
idf.py -DTAB5_UI_NAV_SELFTEST=ON build
```

O build diagnóstico abre SD/HOME cinco vezes e registra listagens e margem de pilha.
Para firmware de uso, desative e reconstrua antes de empacotar/gravar:

```sh
idf.py -DTAB5_UI_NAV_SELFTEST=OFF build
python tools/check_ui_stack.py
```

O ensaio físico da correção concluiu cinco visitas sem reiniciar, com 31 entradas
no SD. O build final 0.2.1 está com autoteste OFF e 12 KB de pilha LVGL.

---

# Regressão 0.2.0 — seis grupos

Executar com dependências do projeto/IDF presentes:

```sh
idf.py build
python tests/run_all.py
python tests/installer/large.py
```

O runner executa validação de parser/pacotes/backups, instalador real simulado,
SD com ordenação global/pastas/editor, registro NVS, RTC (todos os dias de
2000–2099), boot/guarda, modelo e transação de layout e Wi-Fi. Compila a UI LVGL
real e testa HOME, SD, navegação, formulário/CFG, assets PNG/JPEG/blur, barra de
status e 100 ciclos de relógio/wake. Testes de C usam ASan/UBSan conforme o alvo.

Validação física deve seguir SIX_STAGES.md. Conferir especialmente restauração de
dados após backup, cancelamento antes do erase, app padrão+RESET sem ciclo de
boot, RTC sem alimentação, carga/power-off, cartão cheio/ausente e wake sem ativar
botão por baixo. Cortes de energia exigem ensaio próprio; não estão validados.

---

## Histórico de testes anteriores

# Validação reproduzível

## Host

Executar depois de obter as dependências de build:

```sh
python3 tests/firmware/run.py
python3 tests/sd_files/run.py
python3 tests/boot_policy/run.py
python3 tests/installer/run.py
python3 tests/host_wifi/run.py
python3 tests/test_storage_host.py
cmake -S tests/host_ui -B /tmp/tab5-host-ui
cmake --build /tmp/tab5-host-ui -j 12
cd /tmp/tab5-host-ui
./test_wifi
./test_sd
```

Neste macOS, prefixar compilação/testes com
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk` por incompatibilidade
do linker instalado com o SDK 27 padrão. O teste do instalador usa OpenSSL para
adaptar SHA-256 no host (`libcrypto`; Homebrew `openssl@3` neste computador).

| Suíte | Verificação |
|---|---|
| firmware | App real + fixtures; P4 0x12, H2 0x10 rejeitado; revisão, segmentos, entrada, truncamento, checksum/hash, 1500 mutações; UBSan |
| sd_files | Paginação, nomes/caminhos, cópia e bytes preservados, cancelamento sem destino parcial, prévia binária, rename/delete; UBSan |
| boot_policy | Hook real: pedido válido uma vez, retorno no boot seguinte, CRC/slot inválido, erase failure, tabela/partição ausente; ASan/UBSan |
| installer | Parser/instalador reais com SDK/flash simulados: backup integral + manifesto, hash, cancelamento, arquivo alterado, journal/SDK falhando, alvo ativo e security gate; ASan/UBSan |
| host_ui | LVGL 9.3 real: SD/Wi-Fi, geometria, teclado físico/virtual, confirmação destrutiva, consentimento para app-only, respostas atrasadas e 100 ciclos; ASan/UBSan |
| host_wifi | Sequência de eventos atrasados, segunda conexão, NVS, limites e retry AUTH_EXPIRE; ASan/UBSan |
| storage | Caminho de importação e propagação de erro NVS; ASan/UBSan |

## Aparelho

Wi-Fi, digitação A164, conexão, senha persistente e recuperação automática de
AUTH_EXPIRE foram confirmados na sessão anterior. Testes desta nova etapa:

- Boot com novo loader, marca de capacidade e HOME; nomes longos no SD.
- Navegar pastas, paginação, retorno entre telas; selecionar arquivo e segurar pasta.
- Criar pasta, copiar arquivo, conferir conteúdo, renomear, prévia de texto e excluir
  somente arquivos/pastas vazias após confirmação.
- Analisar app-only conhecido e compatível com esta revisão de Tab5; rejeitar .bin
  de C6/S3/H2, truncado, checksum/hash alterado e arquivo merged/backup.
- Conferir plano e backup; cancelar antes/depois de iniciar escrita e verificar
  recuperação. Instalar apenas em slot permitido e não tocar partições de dados.
- **Confirmado nesta sessão:** usuário instalou/iniciou microcity 0.11.0 e confirmou
  retorno ao launcher por RESET. Repetir com outros firmwares para ampliar a matriz.
- Testar cartão cheio/removido e falta de energia em cada etapa, em dispositivo
  de teste com backup externo. Esses ensaios físicos ainda não foram demonstrados.

Compilar ou testar com doubles não equivale a executar esses casos no Tab5.
Merged/multipartes, layout automático e dados por app ainda não têm testes nem implementação.

## Progresso / dois apps / teclado / energia (11/11/2026)

Host com LVGL 9.3.0 e ASan/UBSan: `test_nav`, `test_home`, `test_sd`,
`test_wifi`. Enter deve gerar CLICKED real; setas/Tab mudam foco; habilitar
botão após checkbox deve permitir alcançá-lo; Escape fecha modal e restaura foco.
HOME deve mostrar dois slots, iniciar o escolhido e nunca remover apenas por
abrir/fechar o diálogo. Respostas de páginas antigas são descartadas.

`tests/installer/run.py`: desinstalação recusa digest alterado, app ativo,
security gate e cancelamento antes de apagar. Após iniciar erase, termina todos
os blocos mesmo se callback pedir cancelamento; limpa default/guard do slot.
`tests/multiboot/run.py`: recusa backup incompleto, app corrompido, tabela alterada,
metade superior ocupada e alteração de partições de dados.
`tests/power/test.c` com `power_model.c`: unidades INA226, corrente com sinal,
estimativa 2S/clamps, leitura indisponível e repouso sem inferir USB.

No aparelho: confirmar barra durante instalação real, dois apps após instalar no
slot livre, remoção com backup, teclado em todos os menus e leitura da bateria
com carga/descarga. Compilação/simulação não substituem esses testes físicos.

## 0.3.0: serviços novos

- Abrir/fechar OTA, WEB, FLASH, PI e DIAG repetidamente; observar heap/stack/reset.
- WEB: token incorreto/expirado, oito tentativas, upload cancelado, nome com acentos,
  traversal/NUL, envio/download, parar/reiniciar servidor e conferir revogação.
- OTA: hora inválida, certificado inválido, HTTPS→HTTP recusado, até cinco redirects,
  arquivo truncado/tamanho errado/hash errado, multipartes completos e cancelamento.
- USB: fechar monitor, usar tools/tab5_usb.py; hash no envio, offset errado, abort,
  reconexão, backup, screenshot, download sem sobrescrever arquivo do computador.
- CFG: JSON compacto/escapes, tipo/range/duplicata/unknown/deep inválidos; exportação
  e importação preservam campos, credenciais e tabela; tema não altera rede.
- DIAG: teclado com Ctrl/Shift/Alt, Esc encerra; redetectar, reiniciar driver e C6,
  conferir log/screenshot exportados. O worker de Wi-Fi precisa retornar READY/FAILED.
- Pi indisponível durante desenvolvimento: não declarar teste físico. Fazer
  HELLO/STAT/hora/BYE/hot-plug quando houver Pi e confirmar versão compatível.
- As funcionalidades acima ainda não foram exercitadas no aparelho nesta revisão.
