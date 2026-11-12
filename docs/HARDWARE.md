# Tab5 Launcher — Hardware Reference

Audited from: M5Stack Tab5 docs (2026-11-11), UNIZERSION l5_board.h (da5aa984),
bmorcelli Tab5 platformio.ini (2f43aa7b), M5Stack BSP 1.3.2 source.

## ESP32-P4 (main processor)

- No native Wi-Fi or Bluetooth
- 16 MB flash (SPI), 32 MB PSRAM (octal, 200 MHz)
- Silicon revision: Tab5 ships below v3.0 → `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`

## ESP32-C6 (Wi-Fi coprocessor)

- Connected via SDIO (ESP-Hosted 2.12.6)
- SDIO pins: CLK=GPIO12, CMD=GPIO13, D0=GPIO11, D1=GPIO10, D2=GPIO9, D3=GPIO8, RST=GPIO15
- Powered by PI4IOE GPIO expander (WLAN_PWR_EN, bit 0)
- Ships with ESP-Hosted slave 2.12.6 (later units); early units had 1.4.1
- Restore firmware: see M5Stack docs "Tab5 C6 Wi-Fi restore"

## Display

| Revision | Panel       | Touch      | Notes                           |
|----------|-------------|------------|---------------------------------|
| V1       | ILI9881C    | GT911      | First production run            |
| V2       | ST7123      | ST712x     | Later units (from ~Oct 2026)    |

- MIPI-DSI, 720×1280 physical (landscape = 1280×720 logical)
- PPA rotates logical 1280×720 into the physical frame
- 3 DPI DMA buffers; ISR must be IRAM-safe during flash erase

## I²C Devices (BSP system bus, port CONFIG_BSP_I2C_NUM)

| Device     | Address | Function                    |
|------------|---------|-----------------------------|
| GT911/ST712x | 0x14 or 0x5D | Touch controller     |
| RX8130CE   | 0x32    | RTC (always stores UTC)     |
| INA226     | 0x40    | Battery voltage + current   |
| PI4IOE 5V0674 | 0x44 | GPIO expander (charger, C6, USB5V, power-off) |

## Tab5 Keyboard A164 (SKU A164)

- Connects to Ext.Port1 (internal 10-pin connector J9)
- **Own** I²C bus: SDA=GPIO0, SCL=GPIO1, 100 kHz (I²C port 0, NOT the BSP bus)
- INT: GPIO50 (falling edge, active-low)
- I²C address: 0x6D
- Modes: Normal (bitwise per-key), Character, HID (1 event/key: modifier + usage ID)
- Launcher configures HID mode on init; clears stale events from previous app

## PI4IOE 5V0674 GPIO Expander (0x44)

| Bit | Signal      | Direction | Function                            |
|-----|-------------|-----------|-------------------------------------|
| 0   | WLAN_PWR_EN | Output    | ESP32-C6 VCC (off at power-on)      |
| 3   | USB5V_EN    | Output    | USB-A 5 V rail (Raspberry Pi)       |
| 4   | PWR_OFF     | Output    | Power-off pulse                     |
| 6   | CHG_STAT    | Input     | Charger status                      |
| 7   | CHG_EN      | Output    | Battery charger enable              |

## microSD

- SDMMC slot 1 (not SDIO; independent of ESP-Hosted)
- Pins managed by BSP
- FAT32 or exFAT; never formatted automatically on mount fail

## USB

- USB-C: flashing, serial console, USB-JTAG
- USB-A: host port (keyboard/mouse HID, Raspberry Pi supply)
- Routing details: see Tab5 schematic; not all USB-C/A modes are simultaneously available

## Power / Battery

- 2S LiPo pack; bus voltage via INA226
- Charger enable: PI4IOE bit 7 (CHG_EN must be set at boot, as Aurora Player does)
- Hardware power-off: pulse PI4IOE bit 4
- expander state survives P4 RESET (but drops on power-off pulse)

## Implementação de RTC/energia — 0.2.0

- RX8130CE no endereço 0x32: calendário UTC em 0x10–0x16; VLF em 0x1D bit 1
  invalida a hora. Codec valida BCD, dias, anos bissextos, weekday one-hot e faixa
  2000–2099. Ajuste para calendário, retoma mesmo em falha e limpa VLF por W0C.
  INIEN habilitado; carga da bateria de backup permanece desabilitada.
- Expansor BSP IOE1: G7 controla CHG_EN; G4 produz PWROFF_PULSE. O driver usa o
  handle do BSP e não recria o barramento. USB pode manter o aparelho alimentado.
- INA226/cache SD permanecem no worker; espaço livre agora vem de FATFS e falha
  vira indisponível, não zero. UI mostra GiB com uma casa decimal.
- Leituras anteriores de INA226/A164/SD foram observadas no aparelho. O novo RTC,
  controle de carga e power-off só têm build/host nesta revisão; validação física pendente.

Referências primárias consultadas em 11/11/2026:
[Epson RX8130CE](https://download.epsondevice.com/td/pdf/app/RX8130CE_en.pdf),
[M5Unified RX8130](https://github.com/m5stack/M5Unified/blob/master/src/utility/rtc/RX8130_Class.inl),
[M5Unified Power](https://github.com/m5stack/M5Unified/blob/master/src/utility/Power_Class.inl)
e BSP Tab5 1.3.2 fixado no projeto. Implementações próprias; decoders PNG/TJPGD
continuam nas dependências LVGL com seus avisos/licenças originais.
