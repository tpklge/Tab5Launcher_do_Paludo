# Wi-Fi connection and persistence regressions

Run the production `wifi_link.c` and `wifi_store.c` with event/driver/NVS doubles:

```sh
python3 tests/host_wifi/run.py
```

ASan and UBSan verify late disconnect acknowledgments before reconnect,
second attempts after AP rejection, scan preserving connected/IP state,
DHCP timeout cleanup, bounded AUTH_EXPIRE retries (10/20 s), missing acknowledgments, password/SSID bytes and auth mode,
NVS write/commit failures, most-recent network ordering, bounded retention,
duplicate updates, forget and malformed legacy counts.

On this Mac, prefix with
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`.
These are host tests; actual radio and power-cycle behavior require the Tab5.
