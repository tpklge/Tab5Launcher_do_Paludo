# 0.4.1 — lançamento após instalação e pausa do Wi-Fi

- O botão Iniciar aplicativo do SD usava JOB_LAUNCH, mantendo ui_sd_busy=true
  enquanto a preparação de boot recusava qualquer operação de SD. O lançamento
  agora usa a mesma tarefa assíncrona da HOME, depois que a instalação terminou,
  sem entrar na fila nem marcar a si próprio como ocupado.
- A preparação esperava até 2 segundos pelo estado DISABLED. O evento assíncrono
  de desconexão podia substituir esse estado por READY depois de esp_wifi_stop.
  Agora a pausa possui identificação de solicitação e confirmação do worker,
  limitada a 15 segundos; erro real do SDK é propagado. Eventos tardios de
  desconexão não reativam o estado do Wi-Fi pausado.
- Quando uma preparação falha e o Wi-Fi é retomado, a rede ativa anterior volta
  a ser conectada. Não é necessário redigitar credenciais.
- A atualização grava somente o launcher em 0x20000. Não altera tabela, apps,
  dados dos apps nem arquivos da WebGUI.

Regressões: UI real LVGL verifica que Iniciar após instalar não ocupa a fila SD;
falha de lançamento retorna aviso. Teste do estado de conexão injeta desconexão
atrasada durante pausa. Suíte completa host com ASan/UBSan, compilação ESP-IDF,
limites de stack/RAM e validação física registrados em BUILD_0.4.1.json.

## Validação no Tab5

Flash do launcher conferida por hash. Via console, lançamento real do slot 0:
`Pausa Wi-Fi confirmada: ESP_OK`, seguido de `restarting into slot 0 (app0)`.
Aplicação instalada identificada como `tab5_meshtastic_v2`, versão
`fcdfe23-dirty`; serial confirmou `init done` e sessão de configuração concluída.
Não foi necessário reinstalar nem modificar a aplicação.
