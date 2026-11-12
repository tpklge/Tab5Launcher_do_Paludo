# 0.7.1 — USB, descanso e temas

- USB-C MSC: buffer de transferência aumentado de 512 para 2048 bytes. O disco passa a enumerar antes da inicialização da interface, e o atraso anterior à entrada foi reduzido. Continua usando a ligação física USB-C Full Speed; isso não transforma a porta em High Speed.
- Retorno USB: saída solicitada por botão/comando é processada pelo fluxo principal, sem criar tarefa temporária. O dispositivo MSC é desconectado e o driver encerrado antes de restaurar a console, permitindo ao computador reconhecer a troca.
- Transição USB: backlight desligado e retido no GPIO durante o reinício controlado; a próxima inicialização libera a retenção com PWM em zero. Tela do modo disco só é iluminada após renderizar o primeiro quadro completo. Não é uma troca a quente: mantém o boot exclusivo para evitar acesso simultâneo ao FAT pelo computador e launcher. Ejetar no computador continua necessário.
- Descanso: hora ampliada, dia da semana e data por extenso em português em duas linhas, com fonte que contém os acentos. Exemplo: Quinta-feira / 12 de novembro de 2026. Preserva os quatro estilos, fundos, dim e despertar pelo primeiro toque/tecla.
- Cinco temas adicionais: Ocean, Forest, Sunset, Lavender e Sand, além dos quatro anteriores. Seleção, importação/exportação e configuração por WebGUI aceitam os nove nomes; a cor de destaque personalizada é preservada.

Pendências atuais em [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md). A inspeção visual da transição no painel não pode ser comprovada por uma captura de tela isolada.
