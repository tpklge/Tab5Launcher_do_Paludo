# 0.7.2 — despertar do descanso (preparada, ainda não gravada)

O usuário relatou falta de resposta no descanso, com a hora ainda avançando; após RESET voltou a responder. Isso não demonstra um travamento geral. A serial também registrou brownout em um reset; não há evidência para atribuir a falha de entrada à queda de tensão.

Foi reproduzido em LVGL real um caso de despertar em modo de entrada EVENT, igual ao utilizado pelo BSP: ao excluir o overlay durante PRESSED, o handler retorna antes de retomar o timer que detecta RELEASED. Se o controlador não gerar uma nova interrupção de soltura, a entrada pode permanecer aguardando a liberação. O callback agora retoma explicitamente o timer de leitura; LVGL o pausa na soltura. O primeiro toque continua sendo consumido, sem clicar no controle sob o descanso.

Teste antes do ajuste falhou na asserção de timer ativo após o despertar. Depois do ajuste, passaram 16 combinações de toque/soltura em quatro posições e quatro estilos, sem clique atravessando o overlay, além de 100 ciclos de descanso. Hora/data iguais não são mais realocadas e redesenhadas a cada 250 ms; os ponteiros analógicos só mudam ao trocar o minuto. Invalidações de texto inalterado são verificadas em teste.

A versão instalada foi mantida durante a observação solicitada pelo usuário. Validação física do ajuste fica pendente de sua gravação depois dessa observação.
