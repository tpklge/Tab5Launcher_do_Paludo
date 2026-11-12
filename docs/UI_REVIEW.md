# Revisão 0.3.1

Renderização real do LVGL em host e páginas SD executadas no Chrome com API de teste:

- [CFG: três colunas, paleta e botão de desligar](ui/cfg-0.3.1.png).
- [WebUI desktop](ui/web-desktop-0.3.1.png).
- [WebUI celular](ui/web-mobile-0.3.1.png).

Os arquivos HTML/CSS/JS foram também publicados e relidos no microSD do Tab5,
com hashes comparados. Screenshots acima não são fotografia do aparelho nem
prova de toda a API em hardware. Viewports adicionais testados: 768 e 320 px.

---

# UI 0.2.0

Renderização da UI LVGL real em host (não captura física do Tab5).
HOME tem ações primárias destacadas, secundárias neutras e remoção em contorno
vermelho. Daylight calcula contraste do texto em botões; status usa a paleta atual.
Temas são reaplicados a widgets criados após respostas assíncronas. O overlay de
relógio preserva sua paleta e deixa a primeira tecla/toque apenas despertar.

Testes: HOME, SD, Wi-Fi, CFG/formulários, barra, PNG/JPEG/blur e relógios; ver
`tests/run_all.py`. As renderizações atuais estão em `docs/images/*-0.2.0-host.png`.
A revisão física anterior foi validada pelo usuário para iniciar ambos os apps;
o novo visual/controle ainda precisa de teste no aparelho.

---

## Histórico

# UI / input corrections — 2026-11-11

Visual references inspected:
- https://github.com/UNIZERSION/tab5-launcher
- https://github.com/UNIZERSION/tab5-launcher/blob/main/docs/images/wallpaper-home.png
- https://github.com/UNIZERSION/tab5-launcher/blob/main/docs/images/cfg.png

Independent LVGL implementation uses the references' wide sidebar, dark raised
plates, red accent, large central app cards, and selected menu outline.
No reference artwork or source files were copied into the firmware.

The Wi-Fi list uses the measured content height instead of subtracting pixels
from LVGL's encoded percentage value. Approximately 6–7 network rows fit, with
scrolling for the remaining results. Security labels are retained as data rather
than parsed from display text. WPA2/WPA3 mixed networks open the password dialog;
unsupported enterprise modes give an explicit message.

Password entry supports the A164 physical keyboard, Enter, Escape, backspace,
Delete, Home/End and cursor movement. Virtual keyboard is optional when the
physical keyboard is present. Check/cancel on the virtual keyboard hides it.
The dialog remains above it with Connect and Cancel visible. Passwords are never
printed in logs; SSID and password spaces are preserved.

Keyboard register source (protocol, no code transplant):
https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW/blob/main/code/Keyboard_APP/Core/User/i2c/user_i2c_reg.h
Mode 0x10, interrupt config/status 0x00/0x01, event count 0x02,
HID event 0x30, identity 0xFF. Recovery is executed by the I2C worker.

LVGL was actually 9.6.0 due to an open dependency range; it is now pinned to
9.3.0, the documented baseline. Worker replies go through a bounded queue and
only the LVGL timer updates the UI. Menu navigation is deferred until the current
input event finishes, avoiding destruction during the navigation callback.
These changes address lifecycle risk; absence of device crashes needs observation.

Host renders of the actual screen (network data simulated):
- [Network list](images/wifi-list-host.png)
- [Password dialog](images/wifi-dialog-host.png)

Validation: `tests/host_ui` checks real geometry, input, queue generation/lifetime
and 100 navigation cycles with ASan/UBSan. See its README for reproduction.

## SD — próxima etapa implementada

Referência visual adicional inspecionada:
https://github.com/UNIZERSION/tab5-launcher/blob/main/docs/images/sd.png

Painel de arquivos à esquerda, detalhes/ações à direita, seleção vermelha,
linhas altas, breadcrumb, paginação e botões com profundidade. São renderizados
pelo mesmo `ui_sd.c` do firmware, com dados de teste no host:
[SD](images/sd-browser-host.png) · [Plano](images/sd-install-plan-host.png).
Não são capturas físicas. O worker e o parser/instalador têm testes separados.
