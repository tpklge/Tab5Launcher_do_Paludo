# Local USB MSC override

Espressif usb_host_msc 1.3.0, upstream commit 8ab282ac6cde1970b660fc032b2d33a9c70e8d07. Apache-2.0; see LICENCE. Local component with the same name overrides the managed dependency.

Retains project sector-size validation. Allocation-safe transfer replacement in msc_host.c prevents dangling device->xfer after failed DMA allocation; diskio_usb.c splits reads/writes into <=4096-byte sector-aligned commands to bound exFAT cluster memory demand. No automatic formatting.
