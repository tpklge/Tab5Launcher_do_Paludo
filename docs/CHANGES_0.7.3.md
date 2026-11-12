# Correções críticas — 0.7.3

Versão compilada e preparada, **sem flash**. A versão instalada permanece 0.7.1.
Inclui a correção de despertar preparada na 0.7.2.

- **A01:** recuperação validada de configuração/registros reconcilia `.previous` e `.tmp`, permitindo salvar novamente. Candidatos conflitantes são preservados como `.recovered-N`; cópias inválidas não são descartadas automaticamente.
- **A02:** lançamento manual exige a política de recuperação registrada e consulta novamente o estado depois da preparação. Instalação interrompida, fase desconhecida e falha de leitura bloqueiam o boot.
- **A03:** coordenação comum das operações de armazenamento, com escritores serializados e reserva exclusiva antes de lançar apps, entrar no USB disco ou desligar. Operações existentes terminam antes da transição; novas operações são recusadas. Upload serial mantém a reserva até fechar o arquivo. Mudanças da tabela selam novas operações até reiniciar. A solicitação USB só é persistida depois da reserva e da ejeção de D:; erros liberam a reserva e são expostos no status.
- **A04:** editor recusa sobrescrever um `.previous` preexistente, usa staging exclusivo e verifica novamente a prévia antes de publicar. Remove apenas os auxiliares criados pela própria edição.
- **A05:** erros de leitura e estruturas desconhecidas da guarda/journal da flash bloqueiam a operação em vez de serem interpretados como estado limpo.

`python3 tests/run_all.py` passou: regressões existentes, recuperação por estados simulados de arquivos, preservação do arquivo vizinho, falhas de guarda/journal, bloqueio de boot, threads reais para coordenação e oito cenários do controlador USB. ASan/UBSan habilitados nos testes que os configuram. Build ESP-IDF, orçamento de DRAM e pilha da UI aprovados; tamanho e SHA em [BUILD_0.7.3.json](BUILD_0.7.3.json).

Não houve ensaio físico, corte de energia nem gravação do Tab5 nesta etapa. Há aviso do compilador sobre frame HTTP de 5320 bytes em tarefa configurada com 16384 bytes; medir margem real de pilha no aparelho. Recuperações arquivadas não têm limpeza automática. A06–A13 da auditoria continuam pendentes, inclusive as limitações específicas de concorrência/configuração, caminhos e ciclo de mídia descritas ali. O pacote inicial em `dist` não deve substituir a tabela dinâmica existente; uma futura atualização deve preservar o mapa instalado.
