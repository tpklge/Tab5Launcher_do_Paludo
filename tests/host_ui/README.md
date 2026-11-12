# Regressões LVGL reais

Usam LVGL 9.3.0 fixado no projeto e as telas/serviços de produção com pequenos
substitutos de RTOS, rede e hardware. ASan/UBSan habilitados.

```sh
cmake -S tests/host_ui -B /tmp/tab5-host-ui
cmake --build /tmp/tab5-host-ui -j 8
```

Alvos: `test_nav`, `test_home`, `test_sd`, `test_wifi`, `test_cfg`, `test_visual`,
`test_status` e `test_assets`. Este último recebe PNG e JPEG de teste. Para rodar
todos com os argumentos corretos: `python tests/run_all.py`.

No computador atual, o linker requer
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.
O runner detecta esse caminho quando presente.

Cobertura: Enter real (CLICKED), Tab/setas, checkbox/consentimento, foco modal,
Escape, campos físicos, dois apps, remoção, geração de tela, configurações e data
local inválida, bloqueio de energia em operação crítica, leitura livre/unknown/zero,
contraste Daylight, PNG/JPEG/blur, quatro relógios/dim/off e consumo do wake.
Testes repetem 100 ciclos de telas/assets/overlays onde indicado.

`*.ppm` são renderizações do mesmo código de UI, não screenshots físicos.
I2C, touch, SDIO, corte de alimentação e RTC físico exigem testes no Tab5.
