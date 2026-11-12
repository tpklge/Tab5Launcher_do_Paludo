# 0.5.0 — migração, perfis de dados e OTA com retomada

Implementa os itens 1 a 3 solicitados, mantendo o launcher e o bootloader como
ponto de recuperação. Gravação da atualização: somente o app factory em 0x20000;
a instalação da versão não reparticiona nem modifica os aplicativos existentes.

## 1. Preservar apps ao redimensionar e restaurar flash

Em FLASH, o editor continua limitado ao banco de aplicativos de 8 MiB, com 1 a 4
slots. Antes de confirmar, verifica as imagens e todos os bytes programados de
cada slot, procura uma atribuição em que todos caibam e prende o plano aos hashes
das imagens/tabela. Se qualquer app não couber ou estiver inválido, recusa sem apagar.

A aplicação salva uma imagem completa de 16 MiB, relê e verifica SHA-256. Copia
os apps desse backup para os novos endereços, verifica os bytes e publica a nova
tabela por transação espelhada. Metadados/ícones/origem e perfis seguem a identidade
do app. O app padrão é desativado para revisão após a mudança de slots.

O registro SD `/launcher/private/flash-operation.dat` e a guarda física no setor
reservado 0x15000 mantêm apps bloqueados se houver interrupção, inclusive se o SD
estiver ausente. FLASH / Retomar reexecuta a mesma operação a partir do backup,
sem confiar no banco parcialmente gravado. O plano completo também fica em um
registro imutável no SD, identificado pelo hash da guarda física; isso impede
usar acidentalmente metadados antigos durante a retomada. O bootloader de recuperação já presente
continua usando 0x13000/0x14000 para recuperação da tabela.

FLASH / Backup integral cria `/backups/flash-<id>.full` e `.full.sha256`. Para
restaurar, use FLASH / Restaurar e selecione o `.full` no gerenciador SD; ou abra
o arquivo no SD e use Analisar/restaurar. Exige confirmação e faz outro backup
integral antes de escrever. Apps, dados, NVS e tabela retornam ao estado gravado;
registros transitórios de boot/recuperação não são reativados do snapshot.

**Restauração pela tela exige bootloader/launcher idênticos aos atuais e a mesma
infraestrutura protegida.** Não se apaga o código de recuperação enquanto ele
executa. Para outra revisão, `tools/restore_full_flash.py SNAPSHOT` valida sem
gravar; `--write --port PORT --backup ARQUIVO_NOVO --confirm-sha256 HASH` faz a
restauração integral pelo ROM USB com backup obrigatório. O utilitário verifica
chip P4, tabela, intervalos, checksum/hash do bootloader e app factory, confirmação
do SHA e alteração da origem. Não foi executada restauração destrutiva no aparelho.

## 2. Perfis e restauração automática dos dados

HOME / Dados permite escolher as partições usadas pelo app. Salvar captura os
dados atuais como perfil. A opção de perfil apagado deixa FAT/SPIFFS para o próprio
app inicializar; não presume um formato de filesystem. Sem seleção, dados continuam
compartilhados. Pacotes que declaram partições criam o perfil correspondente;
atualização do mesmo projeto herda as associações anteriores.

Snapshots imutáveis ficam em `/launcher/private/profiles`. Na troca de app, salva
o proprietário anterior, valida todos os arquivos do destino, mantém baseline de
segurança e restaura o perfil antes de liberar o boot. No retorno ao launcher,
captura as alterações do app. Apps sem perfil não são gravados acidentalmente no
perfil de outro app. Registro por identidade impede confusão após mover slots.

A partição principal `nvs` usa exportação por namespace/tipo, cobrindo inteiros,
strings e blobs. `boot_mgr`, `install_state`, `launcher_cfg`, `app_registry`,
`l5_wifi` e `catalog` são reservados e preservados. Não troca a NVS bruta em uso
pelo launcher. NVS com rótulo próprio pode integrar um perfil como partição raw.

A troca possui registro SD, plano imutável e marcador NVS protegido ligado ao
hash desse plano. Índice antigo de perfil não substitui o snapshot já aprovado. Uma troca interrompida é
retomada no boot; FLASH também permite reverter para o baseline anterior. Nenhum
perfil é considerado válido sem flush/releitura/hash. Backups antigos são retidos
no cartão; gestão automática de retenção/limpeza ainda não foi incluída.

Apps antigos/binários isolados precisam da associação explícita: a posse de dados
não pode ser deduzida só do .bin. Os perfis são cooperação entre firmwares, não
isolamento contra código externo que apague a NVS ou regrave a flash livremente.

## 3. OTA com retomada e verificação em lote

Downloads usam diretório determinado pela fonte/id/versão/partes. Mantêm `.part`
após queda/cancelamento e retomam por Range quando a origem permite. Verificam
Content-Range, tamanho, ETag/If-Range e hash; resposta 200 a um pedido Range reinicia
o arquivo, em vez de concatenar dados incompatíveis. Sem SHA publicado, só retoma
com ETag forte; sem validador seguro reinicia. Arquivo completo é relido antes de
publicar. Arquivos finalizados são reutilizados apenas se a integridade conferir.

OTA / Consultar atualizações dos apps agrupa as consultas por fonte e compara
origens/versões registradas. Não usa cache antigo como prova de consulta nova.
OTA / Baixar versões diferentes em lote baixa sem instalar automaticamente.
Versão diferente é candidata, não afirmação de versão mais nova: instalação
continua com análise e confirmação pelo SD.

Instalação de arquivo baixado por OTA registra a origem por identidade. Para apps
anteriores/instalados por SD, nos detalhes do catálogo use Associar origem OTA e
escolha o slot correto. Não adivinha vínculo por nome. Renomear/mover um download
antes de instalá-lo pode exigir essa associação manual.

## Validação

Suíte host ASan/UBSan e LVGL real, com testes novos de migração, restauração,
falha de tabela/retomada, perfis FAT/NVS, namespaces protegidos, interrupção com
registro SD antigo, rollback, Range/ETag e fontes agrupadas. Também corrigido um
uso de ponteiros de cards removidos durante a reconstrução da HOME, encontrado
pelo novo teste de salvar perfil.

Gravação e preflights físicos ficam registrados em BUILD_0.5.0.json. Migração,
restauração e troca de dados destrutivas foram testadas com flash/NVS simuladas;
não foram aplicadas aos dados do usuário para fins de teste.
