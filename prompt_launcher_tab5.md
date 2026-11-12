> Atualização de requisitos — 12/11/2026: prevalece a versão 0.8.0-rc.1. O launcher não usa nem reserva NVS; estado persistente em `/launcher` no SD. Aplicações externas administram suas partições. As referências históricas a NVS neste documento não autorizam reintroduzir essa dependência. Release Candidate em observação prolongada antes de ser final. Veja README.md e README.en.md.

# Prompt de desenvolvimento — Launcher independente para M5Stack Tab5

Copie o conteúdo entre “INÍCIO DO PROMPT” e “FIM DO PROMPT” para o agente de desenvolvimento. Este é um pedido de implementação de firmware, com pesquisa inicial, código, documentação e validação; não apenas de um protótipo visual.

## INÍCIO DO PROMPT

Você é responsável por desenvolver, do zero, um launcher de firmwares exclusivo para o **M5Stack Tab5**. Atue como engenheiro de firmware embarcado, com experiência em ESP32-P4, ESP-IDF, LVGL, ESP-Hosted, USB, sistemas de arquivos, bootloaders e instalações resistentes a falhas.

O nome provisório do projeto é **Tab5 Launcher**. Mantenha nome, versão e identidade visual configuráveis para que o projeto possa receber outro nome sem reestruturar o código.

## 1. Objetivo e referências obrigatórias

Desenvolva uma base própria, organizada e confiável. Estou insatisfeito com bugs dos launchers existentes e quero uma implementação independente que reúna **a união dos recursos dos dois projetos abaixo que fazem sentido no Tab5**, com interface fortemente inspirada no projeto da UNIZERSION:

- https://github.com/UNIZERSION/tab5-launcher
- https://github.com/bmorcelli/Launcher

Não faça um fork renomeado nem transplante grandes arquivos dos projetos. Use-os para compreender comportamentos, formatos, compatibilidade, problemas e soluções. Utilize bibliotecas oficiais e componentes maduros quando adequado: “do zero” se refere à aplicação e à arquitetura própria, não a reescrever drivers e protocolos consolidados. Qualquer reutilização pontual de código ou recursos deve respeitar a licença e aparecer nos créditos.

Antes de implementar:

1. Leia README, documentação, código, configuração do Tab5, bootloader e fluxos de instalação dos dois repositórios. Consulte histórico e issues relevantes para falhas de Wi-Fi, teclado, instalação, OTA, USB e tela.
2. Examine as imagens da interface da UNIZERSION, principalmente carrossel HOME, menu lateral, boot com contagem circular, temas, papéis de parede, configuração e relógio de descanso.
3. Registre branch, commit e data da análise. Não suponha que a descrição pública represente tudo o que o código realmente faz.
4. Crie `docs/FEATURE_PARITY.md`, com uma linha por função encontrada: referência, arquivo/símbolo ou documento de origem, comportamento, aplicabilidade ao Tab5, módulo novo, teste e estado.
5. Separe, nessa matriz, recurso documentado, implementado, experimental e apenas planejado. Preserve a cobertura das funções existentes; uma intenção no roadmap não comprova funcionamento.
6. Acrescente à matriz os requisitos específicos deste prompt, inclusive os que não existem em nenhuma referência.

A matriz é obrigatória para evitar que “incorporar todas as funções” vire uma lista superficial. Funções exclusivas de outras placas não precisam ser portadas; justifique sua inaplicabilidade. Recursos presentes no branch atual, ainda não lançados, entram identificados como tal. A integração opcional com Raspberry Pi faz parte do escopo quando confirmada na referência, sem se tornar dependência do launcher.

Se não puder acessar uma referência, registre exatamente a limitação e continue o trabalho verificável. Não invente uma auditoria nem uma equivalência completa.

## 2. Prioridades e regras de implementação

Prioridade, nesta ordem:

1. Integridade da flash, boot e acesso à recuperação.
2. Estabilidade do Wi-Fi, teclado, microSD e transferência de arquivos.
3. Instalação simples de `.bin`, sem preparação manual de partições pelo usuário.
4. Paridade funcional dos dois projetos no Tab5.
5. Interface, personalização e desempenho.

Não reduza o escopo final a um MVP. Implemente por etapas compiláveis e mantenha os recursos restantes registrados. Evite botões que aparentem funcionar sem ter implementação. Não declare estabilidade, compatibilidade ou validação física com base apenas em leitura, compilação ou simulação.

Tome decisões rotineiras de engenharia e prossiga. Pergunte apenas sobre decisões materiais que não possam ser resolvidas pela documentação, pelo hardware ou por este prompt. Não exija nova autorização para escrever código, executar testes locais e corrigir falhas. Gravar ou apagar o dispositivo físico deve seguir a autorização efetivamente disponível e mostrar previamente o plano de gravação.

## 3. Plataforma e identificação do hardware

O alvo é o Tab5, e não Cardputer, Core, Stick ou um ESP32 genérico.

- ESP32-P4 como processador principal; Tab5 normalmente com 16 MB de flash e 32 MB de PSRAM. Detecte e valide a capacidade real antes de calcular layouts.
- Display de 5 polegadas, área física de 1280 × 720 em paisagem, touch capacitivo e MIPI-DSI.
- ESP32-C6 como coprocessador de conectividade, com ESP-Hosted e transporte utilizado pelo Tab5. O P4 não possui Wi-Fi nativo.
- RTC RX8130CE, alimentação, controle de carga, monitoramento de bateria, codec de áudio, expansores de GPIO, microSD e portas USB conforme esquema oficial da revisão.
- Suporte às revisões de display/touch existentes, incluindo as combinações ILI9881C/GT911 e ST712x quando confirmadas pelo BSP e hardware. Não codifique uma única revisão como se cobrisse todos os Tab5.
- Teclado oficial Tab5 Keyboard A164 em Ext.Port1. Ele é um periférico I²C com interrupção; não é um teclado USB. Como referência documental, endereço padrão `0x6D`, SDA GPIO0, SCL GPIO1 e INT GPIO50; confirme no esquema e na versão do driver antes de usar.
- Teclados e mouses USB são dispositivos adicionais, com driver USB Host próprio.

Comece pela documentação oficial M5Stack e Espressif. Escolha uma versão estável do ESP-IDF que tenha suporte comprovado ao conjunto de hardware e fixe versões compatíveis de BSP, LVGL, esp_lvgl_port, ESP-Hosted e esp_wifi_remote. Justifique a escolha. Use ESP-IDF/CMake como base preferencial; não escolha Arduino apenas para facilitar a cópia dos projetos.

Tenha uma única configuração de inicialização do hardware e documente os proprietários de cada barramento, periférico e canal DMA. Se SDK/BSP exigir correção, mantenha patch pequeno, reproduzível, versionado e explicado. Evite depender silenciosamente de uma instalação local modificada.

## 4. Arquitetura e concorrência

Separe, ao menos, as seguintes responsabilidades em componentes:

- `board`: identificação da revisão, display, touch, GPIO, energia e periféricos.
- `ui`: telas, diálogos, navegação, temas e foco.
- `input`: teclado oficial, touch, teclado virtual e USB HID.
- `wifi`: conexão, ESP-Hosted, coprocessador e recuperação.
- `time`: RTC, relógio do sistema, NTP e fuso.
- `storage`: microSD, arquivos, configuração e política de acesso exclusivo.
- `firmware`: leitura, classificação e validação de imagens.
- `installer`: planejamento e execução compartilhados por SD, rede, WebUI, USB e helper.
- `partitions`: inspeção, alocação, migração e manutenção da tabela.
- `boot`: seleção de app, retorno, contadores de falha e recuperação.
- `catalog`: provedores, favoritos, versões e downloads.
- `backup`: dados por app, partições e cópias da flash.
- `web`: API local e interface web.
- `usb`: host/device, console, transferência e armazenamento em massa.
- `diagnostics`: logs, métricas, relatório e motivo de reset.
- `pi_link`: integração opcional com Raspberry Pi, desacoplada do núcleo.

UI não deve realizar gravação de flash, download, scan de rede, backup ou decodificação pesada em callbacks. Use tarefas, filas e estados explícitos. Defina uma única forma de acessar LVGL: tarefa proprietária ou sincronização prevista pelo port oficial. Workers enviam eventos; não manipulam objetos de uma tela já destruída.

Cada operação longa deve ter ID, estado, progresso, timeout, resultado e cancelamento quando seguro. Centralize instalação para que a WebUI e o console não possam contornar as validações da tela.

Não crie tarefas duplicadas, múltiplos handlers para o mesmo evento ou reinstalações repetidas de driver durante recovery. Documente ordem de init/deinit, posse de recursos, locks e ordem dos locks. ISRs apenas sinalizam trabalho. Não use `abort()` ou `ESP_ERROR_CHECK` para uma falha operacional recuperável, como teclado ausente, C6 indisponível ou cartão removido.

## 5. Interface e personalização inspiradas na UNIZERSION

Quero preservar sua linguagem visual: painel lateral fixo, cartões grandes, carrossel horizontal, bordas e luz de destaque, botões com profundidade, sombras discretas, wallpaper e telas bem organizadas. Crie uma identidade própria, mantendo essa experiência. Não entregue uma interface genérica composta apenas de listas.

### 5.1 Navegação e HOME

- Menu lateral com acesso a HOME, SD, OTA, WebUI, USB, Partições, Configurações e diagnóstico/recuperação. Agrupe ou permita rolar o menu para caber na tela sem reduzir excessivamente os alvos de toque.
- PI aparece quando a integração estiver habilitada; sua ausência não deve atrasar o boot.
- Barra de estado com hora/data, bateria, carregamento, Wi-Fi/RSSI, cartão/espaço livre e status do teclado.
- HOME com carrossel dos apps residentes na flash: swipe, setas, pontos de navegação, teclado e cartão central em destaque.
- Cards com ícone, nome editável, projeto, versão, origem, data de instalação, tamanho e ocupação do slot.
- Ícone alternativo gerado quando faltar imagem. Leitura assíncrona de ícones sem bloquear boot.
- Ações: iniciar, definir padrão, renomear, ver detalhes, associar atalho, backup/restaurar dados e remover.
- Slots vazios com atalhos de instalação, sem exigir conhecimento de endereços de flash.
- Diferencie apps já residentes na flash de binários guardados no SD ou no Raspberry Pi. Um arquivo no SD não deve aparecer como app executável diretamente do cartão.

### 5.2 Boot e interação

- Boot com app padrão, ícone e contagem regressiva circular configurável, botões “Abrir launcher” e “Iniciar agora”, além de atalhos para outros apps.
- Toque ou tecla permite interromper a contagem; operações de transferência também devem suspendê-la.
- Opções de iniciar sempre no launcher, iniciar app padrão, comportamento após deep sleep e acesso rápido ao launcher.
- Foco de teclado visível em todas as telas e diálogos.
- Soltar o dedo fora de um botão cancela a ação; gesto de swipe não pode virar um clique de instalação.
- Primeiro toque/tecla com a tela apagada apenas acorda a tela, sem executar o controle abaixo.
- Diálogos de operação destrutiva devem identificar os apps/dados afetados. Não use confirmações repetitivas em tarefas comuns sem risco de perda.

### 5.3 Temas, imagens e relógio de descanso

- No mínimo Graphite, Midnight, Daylight e Neon, com alternância ao vivo e persistência.
- Cor de destaque fixa, obtida do ícone do app ou do wallpaper, com fallback legível.
- PNG/JPEG no SD, seleção por miniaturas, preview, escolher a imagem pelo gerenciador de arquivos e opção sem wallpaper.
- Ajuste de escurecimento, contraste e escala/corte proporcional; nunca esticar a imagem.
- Limites de tamanho, dimensões e memória explícitos. Formato não suportado deve gerar mensagem, não travamento. Degradação para imagem menor/fundo simples quando faltar memória.
- Temas adicionais importáveis por arquivos de dados, com esquema validado, versão e fallback; tema não executa código.
- Relógio de descanso digital, grande, fino e analógico; fundo borrado, nítido ou preto; prévia imediata.
- Brilho, dimming, tempo até apagar, rotação suportada, volume e sons opcionais da interface.
- Português do Brasil como idioma inicial, com estrutura para inglês e fontes que exibam acentos e símbolos corretamente.
- Compatibilidade documentada, quando viável, com `apps/<project>/icon.png`, `about.txt`, pasta `wallpapers/` e metadados das referências. Importar configuração antiga não deve sobrescrever credenciais ou partições implicitamente.

Não sacrifique a resposta ao toque para manter blur e animações. Defina orçamento de RAM e taxa de atualização mensurável; o modo de efeitos reduzidos deve continuar bonito e legível.

## 6. Instalação sem particionamento manual — requisito central

O fluxo comum deve ser:

**Escolher um `.bin` → analisar → mostrar o que será instalado/substituído → instalar → iniciar ou continuar no launcher.**

Não quero ter que abrir o gerenciador de partições, escolher um esquema, reiniciar e procurar o arquivo novamente para poder instalar. A preparação necessária é responsabilidade do instalador.

“Sem particionamento prévio” significa sem uma etapa manual separada pelo usuário. Não significa executar firmware sem tabela válida nem ignorar as exigências do ESP32-P4.

### 6.1 Modos de uso

**Modo simples: gravar/substituir um app.** Instala o binário selecionado, reutilizando um slot adequado ou propondo automaticamente um layout que o comporte. Preserve launcher e caminho de retorno dentro do modelo de compatibilidade suportado. O usuário não precisa criar slots manualmente.

**Modo multiboot.** Mantém múltiplos apps residentes na flash, instala em espaço livre ou substitui o escolhido, e apresenta tudo na HOME. Calcule quantos apps cabem de verdade, considerando dados e recuperação. Não prometa quantidade fixa para qualquer tamanho de firmware.

**Modo avançado: imagem completa/gravação absoluta.** Analisa imagem merged e manifesto com offsets. No fluxo normal, extraia app e dados compatíveis, preservando a infraestrutura do launcher. Se uma imagem exigir substituir bootloader/tabela/launcher e não puder ser adaptada com segurança, ofereça exportação e instruções para gravação externa, ou uma rotina dedicada executada a partir de ambiente de recuperação comprovadamente seguro. Deixe explícito que gravar uma imagem completa de outro sistema pode remover o launcher. Nunca esconda esse efeito sob “instalar app”.

### 6.2 Formatos e validação

Suporte, com identificação real:

- App-only `.bin` ESP32-P4.
- Merged `.bin` com bootloader, tabela, app e dados, observando a base de gravação do arquivo.
- Pacote multipartes com `bootloader.bin`, `partition-table.bin`, `app.bin`, imagens de dados e manifesto de offsets/tamanhos/hashes.
- Firmware de catálogo com múltiplas fontes; baixar para SD deve preservar todos os componentes necessários, não somente o binário principal.
- Backup bruto de flash, tratado como restauração de backup, nunca confundido automaticamente com app ou merged comum.

Não deduza offsets apenas pelo nome do arquivo nem procure um byte mágico isolado e o trate como prova. Use parser robusto de cabeçalhos, segmentos, tabelas e manifesto, com checagem de limites, alinhamentos, overflow e sobreposição.

Valide chip, revisão mínima/máxima quando disponível, tamanho, checksum, hash, estrutura dos segmentos, capacidade real da flash, layout, requisitos de dados e limitações de segurança. Um hash confirma integridade em relação à referência recebida; não comprove autoria sem assinatura verificável.

Um app-only não descreve necessariamente suas partições de dados, drivers, offsets fixos ou bootloader exigido. Quando essas informações não puderem ser inferidas, use manifesto/metadados, perfil conhecido ou decisão explícita do usuário; não invente compatibilidade. Rótulos de hardware “detectados por heurística” devem mostrar essa incerteza.

### 6.3 Planejamento e execução

O instalador deve produzir um `InstallPlan` antes de escrever:

- Imagem/origem, tipo, app e dados selecionados.
- Layout atual e proposto; regiões lidas, apagadas e gravadas.
- Apps/dados preservados, substituídos ou removidos, identificados por nome.
- Backups necessários, espaço disponível e estratégia de recuperação.
- Necessidade de reboot, dependências desconhecidas e incompatibilidades comprovadas.

Reutilize espaço adequado primeiro. Não reparta toda a flash a cada instalação. Se precisar reorganizar, preserve dados/apps com cópias verificadas quando houver recursos; caso contrário, mostre precisamente o que será perdido.

Inclua “Somente app”, “App + dados necessários” e política equivalente a perguntar/evitar SPIFFS. Não apague SPIFFS, FAT, LittleFS ou NVS de outro aplicativo por conveniência. Nunca copie cegamente a tabela do firmware por cima da estrutura do launcher.

Se uma etapa exigir reboot, persista plano, estado, origem e integridade de forma que a operação continue sem nova seleção do arquivo. Se o arquivo/cartão tiver desaparecido, volte à recuperação com mensagem clara. Não utilize objetos de partição do ESP-IDF obtidos antes de uma alteração de tabela como se refletissem o novo layout.

Flash, validação e progresso devem funcionar com arquivos grandes usando buffers limitados. Só selecione o novo app para boot após terminar e verificar a gravação.

## 7. Boot, recuperação e limites reais de segurança

Projete primeiro o caminho que permite voltar ao launcher após executar outro firmware. Analise as estratégias dos dois projetos e proponha uma implementação própria, justificando se precisa de bootloader customizado.

- O botão RESET deve permitir retornar ao launcher nas instalações compatíveis com o modelo suportado.
- Distinga power-on, reset externo, software reset, panic, watchdog, brownout e wake-up. Não dependa de RAM retida como único estado durável.
- Tenha proteção contra boot loop, com limiar e janela documentados; um app com falhas sucessivas deixa de iniciar automaticamente e oferece diagnóstico, nova tentativa, reinstalação ou remoção.
- Modo de recuperação deve funcionar sem Wi-Fi, SD, wallpaper, teclado físico ou configuração válida.
- Ofereça recuperação por toque e canal serial e documente o uso de BOOT/RESET e gravação externa se a infraestrutura estiver danificada.
- Faça shutdown ordenado de Wi-Fi, transporte SDIO, teclado/I²C, USB, SD, áudio e display antes do reboot, conforme as exigências do hardware. O app seguinte precisa receber os periféricos em estado conhecido.
- Preserve e reative o carregamento quando necessário para o Tab5; não deixe uma sequência de RESET silenciosamente desativar a carga.
- Atualização do próprio launcher deve preservar apps, configuração e dados quando o layout permitir. Inclua pacote de atualização e pacote de instalação inicial claramente distintos.

O rollback nativo do ESP-IDF depende do layout e, em alguns modos, de confirmação pela aplicação iniciada. Firmware arbitrário de terceiros pode não realizar essa confirmação. Não habilite um mecanismo que rejeite todos esses apps, nem declare sucesso de health-check sem evidência. Documente como sua política diferencia o launcher controlado pelo projeto de apps externos.

Alterar tabela de partições, bootloader ou dados não tem automaticamente a mesma resistência a falta de energia de uma atualização de app em slot inativo. Cópia de segurança, gravação do header por último ou arquivo de journal, isoladamente, não garantem recuperação se o bootloader já não conseguir localizar a recuperação. Demonstre a cadeia de boot e teste interrupções em cada etapa crítica. Não prometa operação “à prova de brick”.

Não escreva na partição que está executando. Atualizações de infraestrutura exigem área/ambiente de recuperação e sequência compatíveis, ou ferramenta externa. Não queime eFuses nem ative secure boot, anti-rollback ou flash encryption automaticamente. Se o dispositivo já usa essas proteções, detecte restrições e recuse operações incompatíveis de forma clara.

O launcher não é um sistema operacional com isolamento entre firmwares. Um app externo pode reprogramar recursos persistentes; não prometa impedir isso sem mecanismo real.

## 8. Wi-Fi com recuperação em níveis — requisito central

Implemente um gerenciador com estados claros: desativado, inicializando C6, estabelecendo transporte, pronto, procurando redes, autenticando, obtendo IP, conectado, reconectando, recuperando e falha.

O launcher deve continuar utilizável quando o C6 não responder. Nada de reiniciar indefinidamente, tela congelada ou erro fatal porque a rede está ausente.

### 8.1 Conexão e uso normal

- Buscar redes, ordenar por sinal, atualizar, informar segurança e rede salva.
- Adicionar SSID manualmente, inclusive rede oculta; conectar/desconectar, esquecer uma rede ou todas, habilitar/desabilitar conexão automática.
- Suportar as modalidades de segurança realmente disponíveis na combinação SDK/C6. Não anunciar 5 GHz nem Wi-Fi nativo no P4.
- Preservar exatamente SSID e senha: espaços, maiúsculas, símbolos e bytes permitidos pelo protocolo. Não fazer `trim()` silencioso na senha.
- Campos com mostrar/ocultar senha e teclado virtual acessível mesmo com teclado físico conectado.
- Guardar credenciais somente após a política de validação definida; falha transitória não deve apagar automaticamente uma senha já funcional.
- Informar RSSI, IP, gateway, DNS, MAC, status de internet e versões host/coprocessador.
- Suportar AP local para WebUI e STA; AP+STA apenas quando a pilha escolhida suportar e tiver validação. Mostrar endereço e QR code.
- Seleção de antena interna/externa quando suportada pela revisão e BSP, com opção persistente e explicação curta.

### 8.2 Não confundir falha técnica com senha incorreta

Diferencie transporte/ESP-Hosted, C6 ausente, versão incompatível, SSID não encontrado, associação, autenticação, timeout, DHCP, DNS, ausência de internet, TLS e servidor remoto.

Um timeout de handshake pode ter causas diversas. Exiba “falha de autenticação; confira a senha” quando a causa for ambígua e reserve uma afirmação definitiva para evidência suficiente. Nunca informe “senha errada” por falha de comunicação P4↔C6.

Depois de tentativas limitadas, ofereça recuperação; sem loop infinito. Erros devem registrar etapa, código e ação sugerida, sem revelar senhas.

### 8.3 Ações de recuperação na interface

Inclua em Configurações > Conectividade, também acessível pelo ícone de Wi-Fi:

1. **Tentar novamente:** rearmar tentativa que tenha sido bloqueada após falhas anteriores.
2. **Reiniciar conexão:** encerrar a sessão e reconectar, preservando redes.
3. **Reiniciar driver/transporte:** desmontar e recriar os recursos conforme APIs e sequência suportadas, removendo handlers antigos.
4. **Reiniciar módulo Wi-Fi:** reset físico e/ou ciclo de alimentação do C6 pelos controles reais do Tab5, respeitando polaridade e tempos, e restabelecer ESP-Hosted.
5. **Esquecer redes salvas:** ação separada e confirmada; não é consequência automática das anteriores.
6. **Diagnóstico Wi-Fi:** versões, estado, motivo das últimas falhas e teste de recuperação.

Se a versão da pilha não permitir reinicialização integral em runtime, encerre a tentativa com diagnóstico e ofereça reboot controlado do P4 preservando tudo. Não simule recuperação bem-sucedida. Antes do reset do C6, pare ou avise downloads/WebUI afetados.

Verifique compatibilidade entre host e firmware do C6 por matriz documentada; não force correspondência exata de versão sem necessidade comprovada. Se precisar atualizar o firmware do C6, forneça caminho explícito, pacote correto e restauração documentada. Atualização automática do C6 em todo boot é proibida.

## 9. Teclado com detecção e recuperação — requisito central

O teclado oficial deve funcionar desde o boot e em **todas** as telas, incluindo instalador, senha do Wi-Fi, arquivos, temas, hora, WebUI, partições e diálogos.

- Detectar presença na inicialização e conexão/remoção durante uso, sem depender exclusivamente de uma borda de interrupção que pode ter sido perdida.
- Usar timeout I²C, estado explícito e tentativas limitadas, sem travar UI quando ausente.
- Suportar leitura por interrupção e fallback por polling quando tecnicamente adequado.
- Tratar corretamente Normal/HID/Character e o estado deixado por outro firmware; reconfigurar modo, esvaziar eventos antigos e zerar modificadores presos.
- Setas, Enter, Esc, Tab, Backspace, Delete, Ctrl, Alt, Sym, Aa/Shift e repetição controlada.
- Campos com cursor, edição, início/fim, apagar palavra, seleção/cópia/cola quando aplicável; atalhos de navegação não podem consumir números e letras digitados em campos.
- Mapeamento coerente com as legendas do teclado; composição para português/acentos e símbolos deve ser documentada e testada.
- Atalhos contextuais com tela de ajuda, incluindo trocar abas, copiar/recortar/colar, renomear, criar pasta, buscar e iniciar apps. Ações destrutivas não repetem por tecla mantida.
- Interface completa pelo touch se o teclado falhar. Mostrar estado “ausente”, “pronto”, “recuperando” ou “falha”, evitando alertas intrusivos quando o acessório simplesmente não estiver conectado.

Em Configurações > Entrada, incluir:

1. **Detectar novamente o teclado.**
2. **Reinicializar o teclado:** parar leitura/IRQ, liberar estados e fila, reinstalar a conexão e aplicar configuração.
3. **Recuperar barramento I²C:** apenas com sequência eletricamente válida e controle exclusivo do barramento. Não gerar pulsos ou recriar controladores compartilhados sem considerar os demais dispositivos.
4. **Testar teclado:** pressionamento/soltura, modificadores, modo e firmware quando disponíveis; sair facilmente da tela.
5. Layout, repetição, atalhos e preferência de teclado virtual.

“Reset do teclado” é recuperação real do caminho de entrada. Não invente um pino RESET dedicado nem capacidade de cortar somente sua alimentação: confirme no esquema/protocolo. Use comando de reset do controlador ou power-cycle apenas se existir e for seguro; informe quando reconectar fisicamente for a única recuperação possível.

Reset de Wi-Fi não deve reiniciar teclado, e reset do teclado não deve interromper Wi-Fi/SD sem uma dependência física comprovada. Libere barramentos/modos adequadamente antes de iniciar outro app.

## 10. Data, hora e RTC

Quero acertar o relógio pelo próprio Tab5, inclusive sem internet:

- Tela para editar dia, mês, ano, hora, minuto e segundo, com validação e ajuste por touch/teclado.
- Ajuste manual funcionando offline; sincronização automática por NTP quando habilitada e botão “Sincronizar agora”.
- RTC e relógio do sistema consistentes, preferencialmente armazenando UTC no RTC e convertendo apenas para apresentação.
- Fuso inicial **America/Cuiaba**, atualmente UTC−4, com escolha de outros fusos e implementação documentada das regras aplicáveis. Não aplicar horário de verão brasileiro por suposição.
- Opção 12/24h, formatos de data e indicação da fonte da última sincronização: manual, RTC, NTP ou helper Pi.
- Detectar RTC inválido/perda de alimentação; não exibir data fictícia como sincronizada.
- Persistir fuso e preferências sem SD. Explique limitações reais de retenção do RTC de acordo com a alimentação do hardware.
- Boot offline não deve esperar NTP. Use tempo monotônico para timeouts e duração de operações, de modo que acertar a hora não interrompa downloads ou detecção de boot loop.
- Trate a dependência entre data válida e certificados HTTPS; não resolva relógio errado desabilitando a validação TLS globalmente.
- Arquivos, backups, logs e metadados devem usar timestamps e fuso de forma consistente.

## 11. microSD, arquivos e transferências grandes

Gerenciador completo: navegar, buscar, ordenar, criar pasta/arquivo, renomear, copiar, recortar, colar, excluir, seleção múltipla, detalhes, preview de texto/imagem, editor de texto simples e instalar `.bin`.

- FAT32 e exFAT conforme suporte real da pilha; nomes longos e UTF-8.
- Filtro binários/todos e opção de arquivos ocultos. Não confundir arquivos auxiliares `._*` do macOS com imagens válidas.
- Listas grandes com paginação ou virtualização; sem limite silencioso que esconda arquivos.
- Remontar/tentar novamente, espaço total/livre, mensagens para cartão ausente, removido, cheio, corrompido ou protegido.
- Nunca formatar automaticamente ao falhar a montagem. Formatação, se oferecida, é uma ação destrutiva explícita.
- Cópias e uploads por arquivo temporário, contagem de bytes e hash, com substituição final apenas após sucesso. Documente limites de atomicidade do filesystem.
- Recuperação de transferências parciais, nomes duplicados e falta de espaço; não deixar arquivo truncado marcado como pronto.
- Coordenação explícita entre barramento do cartão e Wi-Fi/ESP-Hosted. Justifique SPI ou SDMMC e valide o uso simultâneo no Tab5.

**Regressão obrigatória:** arquivos na faixa de 3,3 MB e maiores não podem causar tela de erro, panic, watchdog ou reboot. Teste pelo menos 3,3 MB, 5 MB, 8 MB e tamanhos próximos ao limite permitido pelo destino. Para gerenciar/copiar arquivos no SD, teste também tamanhos maiores que um app possível na flash.

Não carregue arquivo inteiro, corpo multipart ou merged na RAM para transmitir/gravar. Use streaming, buffers limitados e backpressure, com progresso em bytes. Inclua arquivos sem tamanho anunciado, quando o protocolo permitir, e rejeição clara antes de esgotar recursos.

## 12. Catálogo, OTA e downloads

- Catálogos utilizados pelas referências, incluindo LauncherHub/M5Burner quando disponíveis e permitidos, por adaptadores substituíveis.
- URLs próprias e GitHub Releases; fontes configuráveis sem alteração do núcleo.
- Filtro por Tab5/ESP32-P4, revisão quando houver metadados, busca, ordenação por nome/data, recomendações, favoritos, versões, autor e descrição.
- Diferenciar “baixado no SD”, “instalado na flash” e “nova versão disponível”.
- Instalar diretamente quando houver estratégia segura sem SD, ou baixar para instalar depois. Não tornar microSD obrigatório para configuração, HOME, hora ou manutenção de rede.
- Atualizar lista de downloads e baixar todas as novas versões, com política clara; “baixar atualizações” não deve instalar silenciosamente em todos os slots.
- Multipartes integralmente preservados com manifesto, hashes e offsets.
- HTTPS com validação de certificado e tratamento de redirects, rate limit, indisponibilidade e mudanças de API.
- Retomada de download usando Range e validação de identidade do conteúdo; se o servidor não permitir, reinicie explicitamente em vez de concatenar bytes de versões diferentes.
- Retry com limite/backoff e cancelamento; não apagar app funcional só porque a rede caiu durante download.
- Cache com expiração, verificação do espaço, metadados consistentes e funcionamento offline para conteúdo já baixado.
- Sem envio desnecessário de identificadores únicos, senhas ou telemetria oculta.

## 13. WebUI e API local

Implemente WebUI própria e responsiva, utilizável em computador e celular, com funções equivalentes às ferramentas web do bmorcelli e integradas ao mesmo núcleo do dispositivo:

- STA e AP conforme capacidades testadas; mostrar URL, QR code, sessão ativa e meios de encerrar o servidor.
- Autenticação local, credencial inicial aleatória ou pareamento explícito, com expiração e limites de tentativas. Não deixar senha pública fixa.
- Gerenciar SD, upload simples/múltiplo, download, edição de texto, criar/renomear/mover/excluir arquivos/pastas.
- Analisar e instalar firmware pela mesma política usada na tela; selecionar slot ou modo simples, acompanhar progresso e receber erro verdadeiro.
- Listar apps, iniciar, escolher padrão, renomear, fazer backup e remover.
- Configurar aparência, rede, entrada, hora e energia quando aplicável.
- Diagnóstico, logs, captura de tela e status de uma operação longa.
- Inspecionar/exportar e editar namespaces/chaves NVS de forma tipada e controlada; backup antes de alterações destrutivas, segredos ocultos e sem sobrescrever namespaces do sistema por engano.

Proteja caminhos contra traversal, valide tamanhos/nomes e autentique ações de escrita. Inclua defesa contra requisições não autorizadas de outras páginas e concorrência entre clientes. Uma única operação destrutiva de flash por vez. Não responda “sucesso” antes de verificar o resultado real.

Se o servidor local utilizar HTTP em rede confiável, documente essa condição e a proteção de sessão implementada; não apresente um código de pareamento como criptografia de transporte. Só anuncie HTTPS local se realmente implementado e testado com seu orçamento de memória.

## 14. USB e console de manutenção

- microSD como armazenamento em massa USB quando o roteamento físico, o controlador e a pilha do Tab5 permitirem.
- Confirme as funções reais das portas e os conflitos host/device; não suponha que USB-C e USB-A sejam intercambiáveis ou suportem todos os modos simultaneamente.
- Ao exportar o SD como MSC, desmonte-o do firmware e bloqueie arquivos, WebUI, instalação e gravações concorrentes. Na saída, aguarde flush/ejeção e remonte.
- Compatibilidade de MSC verificada em Windows, macOS e Linux, ou explicitamente marcada como ainda não testada.
- Ferramenta Python para enviar/receber arquivos via USB/serial, verificar hash, criar/remover diretórios e capturar screenshot PNG, mesmo quando MSC não estiver disponível.
- Console com `help`, status, versões, logs, navegação, apps, partições, configuração, hora, Wi-Fi, recuperação de teclado, instalação, backup, reboot e seleção de boot.
- Parser de argumentos com espaços, aspas e escapes; nunca imprimir senha em logs normais.
- Protocolo de transferência com tamanho, integridade, timeout, cancelamento e resultado. Suspenda autoboot durante sessão ativa.
- Comandos de gravação e partições devem chamar os mesmos serviços e proteções da UI; modo avançado não pode escrever em endereço inválido.
- HID USB Host para teclado/mouse quando compatível com os demais usos da porta.

## 15. Partições, dados dos apps e backups

Gerenciador avançado com mapa de memória e tabela: label, tipo, subtipo, offset, tamanho, ocupação real quando mensurável e proteção.

- Inspecionar, criar, remover, redimensionar e formatar partições permitidas, validando alinhamento, capacidade e sobreposição.
- Perfis para um app grande, múltiplos pequenos, tamanhos mistos e firmware com área de dados maior. Calcule tamanhos após reservar launcher/recuperação/configuração; não copie tamanhos das referências sem provar que cabem na nova arquitetura.
- Mostrar tabelas desconhecidas em modo de inspeção e propor migração controlada; não operar cegamente nem obrigar a apagar tudo só porque o layout não é reconhecido.
- “Verificar imagem/layout” sem escrita, equivalente a sanity check.
- Otimização/compactação preservando apps quando viável com staging e verificação. Se a operação disponível apagar apps, chame-a explicitamente de reorganização com remoção; não sugira preservação inexistente.
- Backup/restauração de FAT, SPIFFS, LittleFS, NVS e dados relevantes, por partição e por app.
- Perfis de dados por app, identificação estável que sobreviva a renomear/mover o arquivo de origem, histórico de backups e restauração seletiva.
- Considere conflitos de labels, offsets fixos e bibliotecas de terceiros que compartilham NVS, inclusive dados de pareamento quando existirem. Não prometa isolamento sem mecanismo real.
- Preserve partições especiais de outros apps quando identificadas; inclua o caso `aurora_cfg`/PIN do Aurora Player documentado pela UNIZERSION, se presente, sem fixar sua existência em todo dispositivo.
- Backup completo da flash para SD/Pi/host com metadados de tamanho, chip, layout, data e SHA-256; mecanismo de restauração externa documentado. Uma cópia da flash P4 não inclui automaticamente o firmware interno do C6.
- Backup automático antes de sobrescrever dados conforme política escolhida. Se configurado como obrigatório e falhar, aborte sem apagar; mostre espaço necessário e destino.
- Retenção configurável, sem apagar a única cópia íntegra para iniciar um novo backup.

Proteja contra edição comum as regiões do bootloader, tabela, launcher em execução, recuperação, configuração do sistema e qualquer região necessária à cadeia de boot. A proteção da UI não substitui validação no serviço.

## 16. Persistência, energia e funcionamento independente

- Configuração essencial em NVS/armazenamento interno dedicado, com versão de esquema, valores padrão, validação, migração e recuperação de corrupção.
- microSD é complemento para assets, backups e exportação; remover o cartão não pode fazer o launcher esquecer Wi-Fi, tema básico, hora/fuso, atalhos ou app padrão.
- Escritas de configuração agrupadas para reduzir desgaste, com tratamento de falta de espaço. Não apagar toda a NVS automaticamente ao encontrar erro.
- Importar/exportar configuração em JSON legível, com credenciais excluídas por padrão e limites de dados validados.
- Reset de aparência, reset de configuração, esquecer redes, recuperação de drivers e restauração completa são ações distintas.
- Atualizar launcher preservando preferências e catálogo/registro dos apps quando compatível.
- Indicadores reais de bateria, fonte e carga, inclusive “USB sem carregar”; não inventar estado por presença de USB.
- Modo de carga, brilho, dimming, screen-off, suspensão/deep sleep e desligamento conforme hardware.
- Antes de operação crítica, avaliar alimentação disponível quando isso puder ser medido com confiança; não permitir suspensão/desligamento pela própria UI durante etapa irreversível.
- Para periféricos opcionais ausentes, usar fallback e manter inicialização rápida. Nada de aguardar Wi-Fi/Pi/NTP para mostrar a HOME.

## 17. Integração opcional com Raspberry Pi

Audite o recurso PI/tab5link da UNIZERSION e implemente suas funções aplicáveis como módulo opcional:

- Descoberta do helper por USB, handshake com versão/capacidades e estados de conexão.
- Informações de rede/internet, temperatura, uptime e endereço do painel do helper.
- Prateleira de binários no Pi: listar, enviar, baixar, remover e instalar no Tab5.
- Proxy/cache de catálogo e downloads, inclusive quando o Wi-Fi do Tab5 estiver desligado.
- Backups completos, backup automático antes de operações destrutivas, retenção e download pelo navegador.
- Sincronização da hora quando o relógio do Pi for válido.
- Captura de tela, informações dos apps, escolher padrão/iniciar, instalar e controlar backups pelo painel autenticado.
- Shutdown ordenado antes de cortar a alimentação USB e controle de ligar/reiniciar conforme hardware.
- Timeout, cancelamento e recuperação se o Pi estiver ausente, lento, sem serviço ou em versão incompatível.

Preserve compatibilidade com o protocolo existente se ele puder ser obtido, documentado e testado. Se o serviço externo não estiver disponível, implemente e teste o cliente com simulador de protocolo e identifique a integração física pendente; não invente compatibilidade com tab5link. Forneça um companion próprio mínimo se necessário, com código e documentação separados.

O launcher deve oferecer por si próprio WebUI, hora, SD, instalação e recuperação. Ter um Pi não pode ser requisito para suprir uma função central ausente.

## 18. Diagnóstico, memória e estabilidade

- Tela de diagnóstico com revisão da placa/display/touch, flash/PSRAM, heap interno livre e maior bloco, menor heap observado, stacks, motivo do último reset, versões, estado do SD, Wi-Fi, RTC, teclado e USB.
- Log circular limitado, exportável e com níveis configuráveis; senhas, tokens e conteúdo sensível não aparecem.
- Registro de falhas persistente pequeno e rotação; coredump quando habilitado e viável.
- Evento de erro com operação, etapa, código, causa provável e ação de recuperação. Evite telas genéricas sem contexto.
- Contadores para falhas/recoveries de Wi-Fi e teclado, sem excesso de escrita em flash.
- Auditar buffers DMA, memória interna versus PSRAM, alinhamento, cache, pilhas e concorrência entre SDIO, display e flash.
- Evitar grandes arrays na stack, leituras integrais de arquivo, allocations sem limite, vazamentos por troca de tema e `String`/JSON gigantes em caminhos críticos.
- Otimizações devem ser medidas; não remover validações de integridade para ganhar velocidade.
- Widgets de progresso e thumbnails não podem consumir todo o orçamento de memória nem impedir watchdog de ser atendido corretamente.

## 19. Testes e critérios de aceite

Crie testes de host para parser de firmware/merged/manifesto, matemática do layout, limites, máquina de estados, migração de configuração, protocolo e caminhos de arquivo. Use fuzzing/injeção de entradas malformadas nos parsers de maior risco. Não escreva apenas testes que reproduzem o código sem verificar comportamento.

Tenha testes de integração e roteiro físico, incluindo:

| Caso | Resultado exigido |
|---|---|
| Boot sem SD, Wi-Fi ou teclado | HOME/recuperação utilizável; configuração interna preservada |
| Cartão inserido/removido em uso | Reconhecimento/erro recuperável sem panic |
| Teclado ausente e conectado depois | Detectado automaticamente e utilizável |
| Teclado em modo inesperado após outro app | Reconfiguração e retomada sem reiniciar tudo |
| Teclado desconectado com modificador pressionado | Nenhuma tecla/modificador fica preso |
| Recuperação de teclado repetida | Sem duplicar ISR, driver, tarefa ou vazamento |
| C6 não detectado na primeira tentativa | UI continua; “Tentar novamente”/reset recupera quando possível |
| C6 ausente ou versão incompatível | Diagnóstico correto e funcionamento offline |
| Wi-Fi com senha válida e falha de transporte | Não acusa senha errada nem apaga credencial |
| SSID/senha com espaços e símbolos | Bytes preservados no teclado, console e armazenamento |
| Reinício do roteador/queda de sinal | Reconexão limitada e recuperação acessível |
| Scan/download com uso do SD | Sem conflito de barramento, deadlock ou corrupção |
| Upload/cópia de 3,3/5/8 MB e limites do destino | Hash correto, memória limitada, sem reboot |
| Upload incompleto ou SD cheio | Sem arquivo final truncado e sem sucesso falso |
| `.bin` app-only compatível | Instala sem etapa manual de particionamento |
| `.bin` maior que o slot atual | Plano automático; sem procurar o arquivo novamente após reboot |
| Merged e pacote multipartes | App/dados/offsets analisados e instalados conforme plano |
| `.bin` de S3/C6 ou arquivo inválido | Rejeitado antes de gravar |
| Dados de app-only não identificáveis | Limitação explícita, sem layout inventado |
| Selecionar “Somente app” | Dados existentes preservados quando compatíveis |
| Cancelar download/instalação | Cancelamento apenas em ponto seguro, estado consistente |
| Falta de energia em cada etapa crítica | Comportamento e caminho de recuperação demonstrados |
| App externo com crash/boot loop | Retorno ao launcher e bloqueio de autoboot conforme política |
| RESET após app de terceiros | Retorno conforme a matriz de compatibilidade comprovada |
| Atualizar launcher | Preservação de apps/dados/configuração conforme plano |
| Ajustar hora offline e reiniciar | Hora/fuso consistentes, consideradas as limitações do RTC |
| Ajustar hora durante download | Timeouts/progresso continuam corretos |
| NTP/TLS com relógio inválido | Erro/recuperação claros, sem desativar certificados globalmente |
| Tema/wallpaper inválido, enorme ou ausente | Fallback legível, sem crash |
| Uso repetido de temas, telas e recuperação | Sem crescimento contínuo de memória |
| Acordar por touch/teclado | Primeiro evento só desperta a tela |
| MSC + tentativa de acesso local/WebUI | Exclusão de acesso preserva integridade do SD |
| WebUI com clientes concorrentes | Uma gravação crítica por vez e resultado verdadeiro |
| Pi ausente/desconectado no meio da operação | Fallback ou interrupção segura e mensagem correta |
| Sessão longa e dezenas de ciclos instalar/iniciar/retornar | Sem vazamento, corrupção de metadados ou bloqueio |

Teste amostras representativas de firmwares feitos em ESP-IDF e Arduino/M5Unified, apps que usam Wi-Fi, SD, teclado e deep sleep, além de firmwares com dados/offsets específicos. Separe incompatibilidade do firmware de falha do launcher e de limitação da revisão do hardware.

Quantifique boot, latência de interação, uso de memória, velocidade de transferência e tempo de recuperação. Metas devem ser justificadas pelo hardware e medidas no build real; números desejados não são prova de desempenho.

Quando não houver Tab5 conectado, execute tudo que for possível em host/CI e produza roteiro físico. Marque separadamente: implementado, compilado, testado em host e validado no dispositivo. Compilação bem-sucedida não equivale a teste físico.

## 20. Entregáveis e sequência de trabalho

Entregue:

1. Projeto independente e compilável, com componentes separados e código completo.
2. `README.md` com instalação inicial, atualização, uso e recuperação, sem depender de conhecimento tácito.
3. `docs/FEATURE_PARITY.md`, `ARCHITECTURE.md`, `HARDWARE.md`, `INSTALL_FORMATS.md`, `FLASH_LAYOUT.md`, `RECOVERY.md`, `TEST_PLAN.md` e `KNOWN_LIMITATIONS.md`.
4. Matriz de versões de ESP-IDF/BSP/LVGL/ESP-Hosted e firmware do C6, revisões de Tab5 e compatibilidade dos apps testados.
5. Scripts de build, empacotamento, transferência e recuperação para uso em Windows, macOS e Linux quando aplicável.
6. CI que compile o firmware, execute testes de host, verifique o layout/tamanho das imagens e produza artefatos.
7. Binário de instalação inicial merged, binário/pacote de atualização preservando dados e artefatos separados acompanhados de `flasher_args.json`/manifesto e SHA-256. Os offsets precisam ser gerados pelo build escolhido, não copiados de exemplos de outra arquitetura.
8. Lista de dependências/licenças e créditos de código, fontes e ícones.
9. Capturas reais do firmware ou renderização da mesma UI, identificadas como tal. Mockup HTML isolado não conta como implementação do launcher.
10. Relatório final com recursos concluídos, validação realizada, pendências, riscos conhecidos e instruções exatas para experimentar e recuperar.

Organize o desenvolvimento assim, mantendo a paridade completa como objetivo final:

- Etapa 1: auditoria das referências, matriz de requisitos, escolha de stack, orçamento de flash/RAM e arquitetura de boot/recuperação.
- Etapa 2: BSP, UI básica real, configuração interna, RTC, teclado e Wi-Fi com recuperação e diagnóstico.
- Etapa 3: parser/validador, plano de instalação, modo simples e retorno confiável ao launcher.
- Etapa 4: multiboot, dados por app, backups, layouts automáticos e manutenção avançada.
- Etapa 5: gerenciador de arquivos, catálogo/OTA, WebUI, USB e integração opcional Pi.
- Etapa 6: acabamento visual completo, temas/wallpapers/relógios, testes de regressão, documentação e pacotes finais.

No início, apresente de forma breve os achados da auditoria, as decisões técnicas e a primeira etapa. Em seguida, implemente. Não encerre apenas com um plano, uma tela simulada ou uma promessa de continuar. Se o trabalho ultrapassar uma sessão, mantenha progresso e próxima ação reproduzível em `docs/PROGRESS.md`, sem ocultar pendências.

**Critério principal:** eu devo conseguir usar o Tab5 para escolher e instalar um firmware sem preparar partições manualmente, voltar ao launcher, recuperar Wi-Fi e teclado pela interface e acertar a hora, com a experiência visual da UNIZERSION e a abrangência de ferramentas do bmorcelli, em uma base própria e verificável.

## FIM DO PROMPT

---

## Referências conferidas na elaboração

Análise realizada em 11/11/2026. Os commits abaixo registram o estado consultado; o agente que receber o prompt deve verificar atualizações antes de implementar.

| Fonte | Estado consultado | Pontos usados na especificação |
|---|---|---|
| [UNIZERSION/tab5-launcher](https://github.com/UNIZERSION/tab5-launcher) | `da5aa9841f9c052578429fcc261cecaaae36ad1e` | README, GUIDE, DEVELOPMENT, imagens e componentes de hardware/instalação/teclado/Wi-Fi |
| [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) | `2f43aa7bd3ef03e770c0a97135cbcfcc79c15aba` | README, interface Tab5, settings, instalação, backups, catálogo, WebUI, MSC e console |
| [M5Stack — Tab5](https://docs.m5stack.com/en/core/Tab5) | Documentação consultada em 11/11/2026 | Arquitetura P4+C6, memória, RTC, interfaces e controles de alimentação |
| [M5Stack — Tab5 Keyboard](https://docs.m5stack.com/en/tab5/Tab5_Keyboard) | Documentação consultada em 11/11/2026 | I²C, modos, GPIOs e identificação do teclado oficial |
| [M5Stack — exemplos do teclado](https://docs.m5stack.com/en/arduino/projects/tab5/tab5_keyboard) | Documentação consultada em 11/11/2026 | Modos, polling/IRQ e configuração da biblioteca |
| [M5Stack — restauração do C6](https://docs.m5stack.com/en/guide/tab5/c6_wifi_restore) | Documentação consultada em 11/11/2026 | C6 com firmware próprio e procedimento de restauração |
| [Espressif — OTA no ESP32-P4](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-reference/system/ota.html) | Canal stable consultado em 11/11/2026 | Diferença entre atualização segura de app, infraestrutura e rollback |
| [Espressif — tabelas de partições](https://docs.espressif.com/projects/esp-idf/en/stable/esp32p4/api-guides/partition-tables.html) | Canal stable consultado em 11/11/2026 | Estrutura, limites e tipos; confirmar detalhes no SDK fixado para o projeto |

O prompt contém a união funcional identificada, correções solicitadas e requisitos adicionais de confiabilidade. Não afirma que os launchers atuais ofereçam todos os comportamentos propostos, que todos tenham sido testados fisicamente ou que qualquer `.bin` possa ser executado sem adaptação.
