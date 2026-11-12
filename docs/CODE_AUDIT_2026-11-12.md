# Revisão de bugs — 12/11/2026

**Conclusão:** antes de ampliar funcionalidades, priorizar recuperação persistente,
bloqueio de boot durante instalação incompleta e exclusão mútua das operações de
armazenamento. Foram identificados 13 pontos, com evidência e limites abaixo.
Nenhum código de produção foi alterado e nenhum firmware foi gravado nesta revisão.

Base analisada: árvore local preparada como 0.7.2. O aparelho permanece na 0.7.1.
As ampliações canceladas não fazem parte do código analisado. A correção de
despertar em `components/ui/ui_visual.c:35` já existe na 0.7.2 preparada; sua
validação física continua pendente da gravação, não sendo uma correção nova desta auditoria.

Foram revisados os fluxos de boot, instalação/migração/recuperação de flash,
persistência, arquivos SD/USB, interface e filas, Wi-Fi, energia, OTA, WebGUI e
integração Pi. Isso não equivale a uma prova formal nem a ensaio físico de todos
os caminhos, especialmente Pi, cortes de energia e desconexão durante escrita.

**Validação realizada:** `python3 tests/run_all.py` passou, incluindo as regressões
com ASan/UBSan. Ensaios adicionais, exclusivamente no computador e em diretórios
temporários, reproduziram seis comportamentos: recuperação que bloqueia novas
gravações, sobrescrita de arquivo vizinho pelo editor, aceitação de aliases de
caminhos protegidos, cancelamento OTA convertido em sucesso, falha de leitura
tratada como ausência de operação e falta de foco de teclado em modal sobreposto.
Os dois últimos fluxos de controle de OTA/flash foram isolados das funções reais
com substitutos de suas dependências; não representam falhas físicas provocadas.

P1 significa corrigir antes de confiar em operações destrutivas ou em recuperação
após interrupção. P2 significa corrigir para robustez e previsibilidade. P3 afeta
principalmente a qualidade do diagnóstico. A prioridade não afirma que o problema
já ocorreu no aparelho, nem atribui a eles o brownout observado anteriormente.

| ID | Prioridade | Problema | Evidência |
|---|---|---|---|
| A01 | P1 | Recuperação `.previous` pode impedir novas gravações indefinidamente | Reprodução com persistência real em arquivos temporários |
| A02 | P1 | Boot manual não bloqueia instalação interrompida pelo journal | Caminho de chamadas e condições do código |
| A03 | P1 | Troca USB/desligamento não têm barreira única para todas as escritas | Condições e concorrência entre tarefas |
| A04 | P1 | Editor sobrescreve arquivo vizinho `.previous` sem confirmação | Reprodução com `sd_files.c` real |
| A05 | P1 | Falha na leitura da guarda pode liberar operação como se estivesse limpa | Reprodução de fluxo com falha injetada |
| A06 | P2 | Política de caminhos protegidos inconsistente com FAT e uploads | Reprodução dos validadores e leitura do FatFs local |
| A07 | P2 | Configuração compartilhada é lida sem sincronização | Leituras/escritas entre tarefas identificadas |
| A08 | P2 | Ciclo USB tem janela de troca de unidade e perde reconexões | Interleavings possíveis; não reproduzido no hardware |
| A09 | P2 | Perder evento de fim de scan pode deixar Wi-Fi em SCANNING | Envio sem tratamento de erro e ausência de prazo de scan |
| A10 | P2 | Falha de memória pode deixar modo USB solicitado para o próximo reset | Ordem entre persistência e criação da tarefa |
| A11 | P2 | Cache pode transformar cancelamento OTA em sucesso | Reprodução da função de fallback |
| A12 | P2 | Modal sobre outro modal perde navegação pelo teclado | Reprodução com LVGL real |
| A13 | P3 | Captura não inclui descanso, avisos e diálogos da camada superior | API de snapshot e árvore de objetos |

**A01 — Recuperar leitura sem recuperar a capacidade de gravação.**

Referências: `components/storage/storage.c:118`, `storage.c:165`;
`components/storage/launcher_store.c:69`, `launcher_store.c:93`.

A publicação move o arquivo atual para `.previous`, publica o novo e remove a
cópia antiga. Uma interrupção entre essas etapas deixa `.previous`. A leitura
aceita a cópia, mas não conclui a recuperação; a próxima gravação recusa qualquer
`.previous` existente com `ESP_ERR_INVALID_STATE`. Mesmo um arquivo principal
válido pode ficar permanentemente sem atualização se a interrupção ocorrer antes
de remover a cópia antiga.

No ensaio, um registro com valor 11 foi movido para `.previous`, simulando o
estado após a primeira renomeação. A leitura recuperou 11, mas a gravação de 22
foi recusada. Isso afeta configuração, credenciais Wi-Fi e os registros privados
usados pelas transações de flash/perfis. Não significa que toda interrupção de
flash produzirá esse estado, mas impede confiar na retomada nesse ponto de falha.

Correção sugerida: implementar recuperação explícita de publicação sob o mesmo
lock, validando principal, anterior e temporário. Restaurar/publicar a versão
escolhida antes de permitir uma nova gravação; nunca simplesmente apagar a única
cópia válida. Para registros transacionais, usar duas gerações com versão e CRC,
de modo que o leitor escolha a geração válida mais recente.

Teste necessário: interromper antes/depois de cada rename e da remoção da cópia,
reinicializar, ler e gravar novamente. O teste deve verificar tanto os dados
recuperados quanto a capacidade de continuar a transação.

**A02 — Lançamento manual ignora parte do estado de recuperação.**

Referências: `components/installer/installer.c:65`, `installer.c:974`;
`components/boot_mgr/boot_mgr.c:211`; `main/main.c:94`;
`components/ui/ui_home.c:248`; `components/web/web.c:737`.

`installer_recovery_required()` considera o journal NVS com `phase == 1`, além
das transações de flash e perfis. O autoboot consulta esse estado. O caminho de
lançamento manual não faz a mesma checagem: verifica a imagem e chama
`prepare_handoff()`, que consulta tarefas ocupadas; `installer_prepare_app_data()`
bloqueia a guarda de flash, mas não o journal de instalação.

Cenário concreto: o binário de um pacote foi completamente gravado e validado,
mas a escrita posterior de vfs/SPIFFS falhou. A imagem pode continuar válida,
enquanto os dados estão incompletos e o journal permanece em fase 1. O caminho
manual pode iniciar esse aplicativo. A validade do binário não comprova a
consistência dos dados externos.

Correção sugerida: uma política comum de autorização de boot, usada por HOME,
WebGUI, USB e autoboot, consultada com a operação protegida por lock. Registrar
slot, identidade, regiões de dados e etapa interrompida; permitir apenas a ação de
retomada/rollback apropriada. Evitar um bloqueio genérico sem caminho de recuperação.

Teste necessário: falhar após verificar o app e durante cada região de dados;
tentar todos os caminhos de lançamento e só liberar depois de recuperar.

**A03 — Falta uma barreira comum entre escrita, boot, USB exclusivo e energia.**

Referências: `components/web/web.c:771`, `components/ui/ui_cfg.c:73`,
`main/main.c:94`, `main/main.c:151`, `components/energy/energy.c:96`.

A entrada USB pela Web verifica instalador/worker Web/recuperação, mas não uma
cópia ou edição do worker SD nem uma transferência USB serial. O desligamento
manual verifica outros indicadores, mas não toda escrita HTTP/serial. Os checks
de ocupação também não impedem que uma nova operação comece logo depois deles.
O marcador de handoff bloqueia comandos USB, mas não é uma trava comum de todos
os produtores. Operações no SD interno não são contadas pelas referências de
mídia que hoje protegem principalmente D:.

Assim, uma tarefa pode estar escrevendo no cartão enquanto outra aceita reinício
em modo disco ou desligamento. Arquivos temporários reduzem parte do impacto,
mas não garantem publicação nem consistência do FAT durante um reset.

Correção sugerida: coordenador de manutenção com estados NORMAL, QUIESCING e
EXCLUSIVE. Reservar o estado antes de verificar filas, impedir novas escritas,
aguardar escritores e arquivos abertos, sincronizar e então trocar de modo ou
desligar. Leituras de status/progresso podem continuar. Utilizar o mesmo mecanismo
para instalador, boot, SD, HTTP, serial e política de energia.

Teste necessário: iniciar cópia/upload/edição em cada interface, solicitar USB ou
desligamento em outra e verificar recusa ou espera. Exercitar também operações
iniciadas no intervalo entre a primeira checagem e a troca de modo.

**A04 — Edição pode destruir um arquivo que não foi selecionado.**

Referência: `components/storage/sd_files.c:454`.

O editor executa `unlink(path + ".previous")` antes de preservar o original.
Esse nome pode pertencer ao usuário; não há identificação de que foi criado pelo
launcher. No ensaio, editar `notes.txt` substituiu silenciosamente o conteúdo
pré-existente de `notes.txt.previous` pelo texto antigo de `notes.txt`.

Correção sugerida: nome temporário único em área interna reservada ou recusa se
o nome de recuperação já existir sem vínculo comprovado à operação. A regra de
limpeza deve remover apenas arquivos que o launcher criou e identificou.

Teste necessário: criar os possíveis nomes vizinhos antes de editar e comprovar
que nenhum conteúdo é perdido, inclusive em duas edições simultâneas.

**A05 — Estado desconhecido da guarda é tratado como ausência de operação.**

Referência: `components/partitions_mgr/flash_snapshot.c:401`.

Se `esp_flash_read()` falhar, a função continua. Se o registro privado também não
puder ser lido, o retorno é `false`: nenhuma operação pendente. A falta de memória
já é tratada de forma conservadora, mas erro de leitura não. O ensaio de fluxo
injetou erro nas duas leituras e confirmou esse retorno.

Correção sugerida: estado ternário CLEAN/PENDING/UNKNOWN, ou API que retorne erro
separadamente. UNKNOWN deve impedir boot e novas operações destrutivas até que
a infraestrutura seja lida e validada; a interface deve explicar a falha.

Teste necessário: erros de leitura da guarda, CRC inválido, SD ausente e registro
incompleto. Não confundir ausência comprovada de registro com erro de leitura.

**A06 — Caminhos protegidos não têm uma política única e compatível com FAT.**

Referências: `components/web/web.c:544`, `web.c:380`;
`components/web/web_security.c:132`; `components/storage/sd_files.c:47`.

O validador geral permite segmentos terminados em ponto/espaço. A proteção de
`/launcher` compara o prefixo textual, sem normalizar esses segmentos. No ensaio,
`/sdcard/launcher/config.json` foi bloqueado, mas
`/sdcard/launcher./config.json` e `/sdcard/launcher /config.json` foram aceitos pela
mesma política. O `create_name()` do FatFs local remove pontos/espaços finais;
portanto essas grafias podem designar o mesmo diretório em FAT. Não foi feito
ensaio físico desse alias em exFAT; não se presume comportamento idêntico.

Separadamente, o upload protege a pasta `private`, mas permite substituir outros
arquivos em `/launcher` que editar/excluir recusam. Isso permite contornar a
validação de configuração e a política de proteção de backups por uma interface
autenticada. Não é bypass de autenticação.

Correção sugerida: rejeitar segmentos ambíguos na entrada e centralizar autorização
por operação após a resolução de caminho. Permitir publicação de assets e edição
de `ota_sources.json` por rotas explícitas, mantendo configuração e metadados
transacionais sob suas APIs validadas. Aplicar a regra também a destinos de cópia.

**A07 — Configuração é compartilhada por ponteiros sem snapshot consistente.**

Referências: `components/storage/storage.c:195`, `storage.c:267`;
`components/ui/ui.c:419`, `components/ui/ui_visual.c:29`.

`storage_update()` substitui a estrutura sob mutex, enquanto vários leitores usam
`storage_get_config()` sem esse mutex. O ponteiro gravável expõe ainda outra rota
de mutação. Em tarefas concorrentes, a leitura pode combinar campos de versões
diferentes; trata-se também de data race no modelo de memória C. Não foi usado
ThreadSanitizer nesta auditoria, portanto não se afirma uma manifestação específica.

Correção sugerida: remover o acesso gravável e usar snapshots/gerações imutáveis.
Separar o lock curto da configuração em RAM da serialização de I/O: simplesmente
fazer toda leitura da UI esperar o lock atualmente mantido durante fsync pode
introduzir novas pausas visíveis. Testar alterações simultâneas pelo menu e WebGUI.

**A08 — Referência USB não identifica a geração da unidade montada.**

Referências: `components/storage/sd_files.c:25`;
`components/usb_storage/usb_storage.c:34`, `usb_storage.c:70`.

A aquisição verifica um booleano, incrementa um contador e verifica o booleano
novamente. Se uma tarefa parar entre a primeira leitura e o incremento, a unidade
pode ser desmontada e outra montada antes que ela continue. A segunda leitura
voltará a ser verdadeira, mas agora para outra mídia. O booleano não identifica
essa troca. É um interleaving possível, não uma troca de arquivos observada.

Além disso, CONNECTED só é processado quando `device` é nulo. Uma reconexão
durante o fechamento, enquanto ainda houver referência antiga, pode ser consumida
e ignorada; depois de liberar a unidade antiga não haverá outro evento para montar a nova.

Correção sugerida: adquirir referência e estado sob a mesma exclusão usada pela
desmontagem, com geração incrementada por montagem. Manter conexão pendente para
reavaliar após fechamento. Testar desconexão/reconexão durante uma operação longa.

**A09 — Fim de scan Wi-Fi pode ser perdido sem recuperação.**

Referências: `components/wifi/wifi_mgr.c:143`, `wifi_mgr.c:289`, `wifi_mgr.c:361`.

O handler envia CMD_SCAN_DONE sem espera e ignora falha da fila. Se a fila estiver
cheia, o worker não recebe o único evento que limpa o callback pendente e restaura
o estado. O loop de timeout trata reconexão em READY, mas não expiração de
SCANNING. As APIs rejeitam novas conexões/scans nesse estado.

Correção sugerida: sinalização de conclusão que não possa ser descartada por
comandos comuns e deadline de scan com recuperação do C6/driver quando necessário.
Nunca bloquear indefinidamente dentro do handler do event loop. Testar fila cheia
no instante do evento, evento ausente e evento de scan antigo chegando atrasado.

**A10 — Solicitação persistente de USB sobrevive a falha de criação da tarefa.**

Referências: `components/web/web.c:774`; `components/usb_disk/usb_disk.c:36`.

A rota grava `usb_disk=1` em NVS antes de criar `disk_reboot`. Se `xTaskCreate`
falhar, responde 503, mas não desfaz a flag. Um reset posterior pode entrar no
modo disco mesmo depois de a solicitação ter sido informada como falha.

Correção sugerida: reservar previamente a tarefa/coordenador e só confirmar o
pedido depois da persistência; em qualquer falha, desfazer a solicitação. Se o
rollback não puder ser persistido, informar explicitamente o estado indeterminado.
Testar falha de alocação, NVS e criação da tarefa em cada etapa.

**A11 — Cancelamento OTA é convertido em fallback bem-sucedido.**

Referência: `components/catalog/catalog.c:191`.

O download de índice retorna `ESP_ERR_INVALID_STATE` para cancelamento, mas o
fallback para cache é aplicado a qualquer erro e troca o resultado para ESP_OK.
Na atualização forçada com cache existente, Cancelar pode terminar como sucesso
e a agregação pode seguir consultando outras fontes. A reprodução isolada da
função confirmou o fluxo.

Correção sugerida: distinguir cancelamento de falha de rede; propagá-lo antes de
considerar cache. Respeitar também o retorno do callback ao carregar cache. Testar
cancelamento com cache presente/ausente e com várias fontes.

**A12 — Uma segunda janela não assume o foco de teclado.**

Referências: `components/ui/ui_nav.c:70`, `components/ui/ui_form.c:42`;
`main/main.c:156`.

O gerenciador aceita um único modal e ignora o registro de outro. É possível
exibir um aviso, por exemplo de bateria, sobre um seletor ou descanso já aberto.
O novo aviso é desenhado, mas os controles não entram no grupo ativo. Em LVGL
real, a abertura de `ui_notice()` sobre um modal manteve o foco no botão da janela
anterior; Enter não fechou o aviso visível.

Correção sugerida: pilha explícita de modais, ou adiamento de avisos até terminar
a janela atual. Ao remover uma janela, restaurar o modal/grupo anterior com
referências válidas. Testar descanso + aviso, seletor + erro e progresso + aviso,
incluindo Esc e exclusão das janelas em ordens diferentes.

**A13 — Screenshot omite a camada onde está o problema relatado.**

Referência: `components/diagnostics/screenshot.c:24`.

A captura usa `lv_snapshot_take(lv_screen_active(), ...)`. O descanso e diálogos
são filhos de `lv_layer_top()`, não dessa árvore. A imagem pode mostrar a tela
base enquanto o usuário vê relógio ou confirmação, prejudicando o diagnóstico.

Correção sugerida: captura composta das camadas visíveis ou do quadro final,
com limites de memória e sincronização. Enquanto isso, identificar a captura como
"tela base" e registrar no diagnóstico quais overlays estão ativos. Um screenshot
isolado também não comprova ausência de clarão durante reinício.

**Ordem de trabalho recomendada:** A01, A02 e A05 para tornar a recuperação
confiável; A04 para eliminar perda colateral; A03/A08 para coordenar armazenamento;
A06/A07/A09/A10 para política, concorrência e falhas de recursos; A11–A13 para
cancelamento e diagnóstico/interface. Validar separadamente a 0.7.2 já preparada.

A suíte atual tem verificações úteis de limites, SHA, preservação de partições,
rollback, parsing e ciclos de UI. Contudo, os substitutos de RTOS não exercitam
contenção real de todas as tarefas. Recomenda-se acrescentar injeção sistemática
de falhas de filesystem/NVS/flash, testes com threads e locks reais e cenários de
modal sobreposto. Testes host aprovados não certificam resistência a cortes de
energia ou estabilidade prolongada no aparelho.

Evidências locais e reproduções preservadas em `recovery/code-audit-2026-11-12/`.

## Resolução preparada na 0.7.3 — sem flash

Os achados acima preservam o diagnóstico original. A01–A05 foram corrigidos nesta revisão:

| Achado | Implementação principal | Regressão |
| --- | --- | --- |
| A01 | storage/launcher_store.c, storage/storage.c | launcher_store/test.c, test_storage_host.py |
| A02 | boot_mgr/boot_mgr.c, installer/installer.c, main/main.c | boot_runtime/test.c, installer/test.c |
| A03 | storage/storage_guard.c e integração das operações/transições | storage_guard/test.c, usb_mode/run.py, usb/test.c, host_ui/test_cfg.c |
| A04 | storage/sd_files.c | sd_files/run.py |
| A05 | partitions_mgr/flash_snapshot.c, installer/installer.c | flash_snapshot/test.c, installer/test.c |

Build e suíte completa aprovados. [Registro](BUILD_0.7.3.json) e [escopo/limites](CHANGES_0.7.3.md). A06–A13 continuam pendentes; aprovação host não substitui validação física.

A versão 0.7.4 foi posteriormente gravada e validou criação de pastas/leitura/escrita no USB-A: [registro](BUILD_0.7.4.json). Isso não substitui ensaios dos demais cenários de recuperação.
