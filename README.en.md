# Tab5Launcher do Paludo

English · [Português](README.md)

A launcher for the **M5Stack Tab5 (ESP32-P4 + ESP32-C6)**, developed in C with ESP-IDF and LVGL 9. Install and launch firmware, manage files, and configure the device through touch, the physical keyboard, and a responsive WebGUI.

**Version: 0.8.0-rc.2 — Release Candidate (RC).** This version will be used and observed for an extended period before it is considered final. There is no scheduled final release date. Issues found during this period may lead to additional RCs. Passing automated tests does not mean every scenario has been verified on hardware.

## Inspiration and acknowledgements

- [UNIZERSION/tab5-launcher](https://github.com/UNIZERSION/tab5-launcher): inspiration for the Tab5 visual design, menu organization, color palette, graphical flash map, and hardware user experience.
- [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher): inspiration for firmware installation and management, backups, OTA catalogs, and the WebGUI with file and folder management.
- [M5Stack — Tab5](https://docs.m5stack.com/en/core/Tab5) and [Tab5 Keyboard](https://docs.m5stack.com/en/tab5/Tab5_Keyboard): hardware documentation, examples, and board support.
- [Espressif ESP-IDF](https://github.com/espressif/esp-idf), [ESP-Hosted](https://github.com/espressif/esp-hosted-mcu), [ESP-USB](https://github.com/espressif/esp-usb), and [LVGL](https://github.com/lvgl/lvgl): technical foundations and libraries.

These references informed development and interface requirements. This is an independent project, not an official release or an endorsed version of those projects. Component and font licenses and credits are preserved; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Features

- HOME with installed applications, custom names/icons, default boot, and uninstall with optional backup.
- Firmware installation from microSD, USB-A storage, or OTA download, with image analysis, integrity checks, and progress.
- Flash allocation based on actual size, a graphical map, and reorganization with backup and a recovery guard; up to 16 applications when space permits.
- File manager: microSD as **C:** and FAT/exFAT USB storage as **D:**, folders, copy, rename, limited text editing, and confirmed recursive deletion.
- WebGUI with cookie sessions, file/folder drag and drop, per-file progress, retries, folder creation, and automatic listing refresh. HTML/CSS/JS reside on the microSD.
- Wi-Fi networks saved on the card; themes, a 144-color grid, wallpaper, idle clocks, Portuguese date formatting, physical keyboard support, brightness, and power settings.
- Exclusive USB-C disk mode for accessing the microSD from a computer. Requires a controlled reboot and host eject; it cannot run alongside the launcher.
- USB console, diagnostics, application data profiles, and optional Pi/Tab5Link integration. Pi integration still needs hardware validation.

## No launcher NVS

The launcher **does not initialize, read, write, or import NVS namespaces**. Configuration, Wi-Fi credentials, metadata, boot guards, and installation/recovery journals reside under `/launcher` on the microSD. The temporary USB disk request uses an RTC marker consumed once after a software reset.

The default layout for new installations **does not reserve `nvs`, `aurora_cfg`, `vfs`, or `spiffs`**. Applications requiring NVS must supply/create a compatible partition and manage it themselves. Calling `nvs_flash_init()` alone does not add a partition-table entry: firmware assuming an existing shared partition may need adaptation. The launcher does not automatically install arbitrary third-party NVS partitions.

An app-only launcher update does not erase existing application partitions. Existing data partitions can be backed up as raw bytes without opening NVS namespaces. Older logical NVS snapshots are not automatically converted; capture the data again before relying on a new profile. Credentials/preferences already on the SD remain usable; settings stored only in legacy NVS are no longer imported. Keep the card inserted for persistent operations. In the clean layout, unpartitioned space after `0xa10000` is not added to the current allocator capacity; extending that bank requires additional validation.

## Build and test

Use **ESP-IDF 5.4.4**, ESP32-P4 tools, Python, CMake, and a host C/C++ compiler. Dependencies are pinned in [main/idf_component.yml](main/idf_component.yml).

```sh
git clone https://github.com/tpklge/Tab5Launcher_do_Paludo.git
cd Tab5Launcher_do_Paludo
# Activate ESP-IDF 5.4.4 using your installation's export.sh.
idf.py build
python tools/check_startup_memory.py
python tools/check_ui_stack.py
python tools/check_no_nvs.py
python tests/run_all.py
python tools/package_firmware.py
```

Host tests also need OpenSSL headers and libraries, such as `libssl-dev` on Linux or Homebrew `openssl@3`. UI tests use the LVGL source downloaded during the first firmware build. CI runs the build, memory checks, and regressions. Pinned component compatibility fixes are applied during configuration; the fixed MSC driver uses an explicit local copy.

## microSD and WebGUI

Copy the contents of [sdcard/](sdcard/) to the card root, preserving your existing `/launcher` directory and settings. Supplied files include the WebGUI, optional fonts, C6 firmware, and an OTA source example. Building or copying files does not automatically update the C6.

Edit `/launcher/ota_sources.json` manually to configure sources. Connect Wi-Fi and start the WebGUI on the device, then use the displayed address and code. The browser keeps the session in a cookie. Private settings, device backups, and credentials are excluded from the repository.

## Installation and upgrades

Packaging generates `dist/` files, with offsets, sizes, and hashes in `manifest.json` and `SHA256SUMS`.

- **Initial installation:** `tab5-launcher-initial.bin` is a complete image flashed from `0x0`. It can replace existing flash content; make a full backup first.
- **Existing launcher update:** flash the application binary only at the launcher offset and within the reservation verified in the installed table. **Do not flash the table from `build/` or run `idf.py flash` over an existing dynamic layout.**
- If the new launcher does not fit, use [tools/prepare_dynamic_upgrade.py](tools/prepare_dynamic_upgrade.py) with a recent full backup and inspect its plan. The tool prepares files; it does not flash the device.

An app-only update does not remove legacy NVS entries from the installed partition table. The new default layout omits those reservations. Do not upgrade with a pending installation, restore, or profile switch.

## Validation status

On 0.7.4, creating 13 USB-drive directories, writing/reading 256 KiB with SHA-256 verification, and deleting test data passed on a Tab5 without reboot. Version 0.8.0-rc.2 passed a clean installation, boot, Wi-Fi, SD listing, and map/API checks on hardware, in addition to build/host tests. Recovery workflows, profiles, and extended observation still need validation.

Limitations, outstanding work, and historical records: [docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md), [docs/CHANGES_0.8.0-rc.2.md](docs/CHANGES_0.8.0-rc.2.md), and [docs/TEST_PLAN.md](docs/TEST_PLAN.md). Arbitrary `.bin` compatibility, survival of every power cut, and isolation between firmware sharing the same flash are not guaranteed. NTFS, multiple drives/hubs, and Secure Boot/encrypted flash are unsupported in this installation workflow.

## License

[GNU GPL v3](LICENSE). Third-party components and fonts retain their respective licenses and notices.
