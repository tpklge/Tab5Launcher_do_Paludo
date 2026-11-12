# Third-party notices and inspiration

The repository's existing GNU GPL v3 license is retained in LICENSE. Inspiration does not imply endorsement or ownership of upstream projects.

| Project | Role | Notice |
| --- | --- | --- |
| [UNIZERSION/tab5-launcher](https://github.com/UNIZERSION/tab5-launcher) | Visual and interaction inspiration for Tab5 | Linked attribution; see upstream for its own terms |
| [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher) | Firmware management and WebGUI workflow inspiration | Linked attribution; see upstream for its own terms |
| [M5Stack](https://github.com/m5stack) | Tab5 hardware documentation and board support | Dependencies retain upstream notices |
| [ESP-IDF](https://github.com/espressif/esp-idf), [ESP-Hosted](https://github.com/espressif/esp-hosted-mcu), [ESP-BSP](https://github.com/espressif/esp-bsp), [ESP-WiFi-Remote](https://github.com/espressif/esp-wifi-remote), [ESP-USB](https://github.com/espressif/esp-usb) | SDK, drivers, networking, USB | Upstream license headers preserved; managed dependencies downloaded separately |
| [LVGL](https://github.com/lvgl/lvgl) | User interface | MIT; managed dependency, with upstream notices |
| Noto Sans SC / Noto Emoji | Optional microSD fonts | SIL Open Font License notices alongside binaries in sdcard/launcher/fonts |

The local `components/espressif__usb_host_msc` copy is based on Espressif usb_host_msc 1.3.0, upstream commit `8ab282ac6cde1970b660fc032b2d33a9c70e8d07`. Its Apache-2.0 LICENCE and source notices are included. Local changes are documented in its README.

`cmake/dependency_fixes.cmake` contains narrowly scoped compatibility fixes for the pinned versions of ESP LVGL Port, ESP WiFi Remote, and ESP-Hosted. It checks the original/patched file hashes and refuses unknown content. These fixes cover cache-safe display callbacks, the remote Wi-Fi default configuration include, and an ESP-IDF 5.4 Kconfig compatibility issue.

The optional C6 image in `sdcard/launcher/c6` is built from ESP-Hosted/ESP-IDF; its configuration, hashes, and provenance are recorded in the adjacent manifest and sdkconfig.build. It remains firmware for the separate network coprocessor; the launcher's SD-only state policy does not remove that coprocessor's own storage.
