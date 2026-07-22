# HLV player for ESP32-2432S028 — pure ESP-IDF

This is a self-contained ESP-IDF 5.5.5 project for the two-USB CYD board. It
does not use Arduino, LovyanGFX or globally installed Espressif tools.

The only application components are:

- `main`: ST7789 SPI2 DMA, microSD SPI3 DMA, continuous DAC audio and player;
- `hlv1`: a vendored decoder-only snapshot of the portable HLV codec.

`MINIMAL_BUILD` in `CMakeLists.txt` admits only their transitive ESP-IDF
dependencies, excluding Wi-Fi, Bluetooth, networking, NVS and OTA. The
`sdkconfig.defaults` profile also disables coredumps, the task watchdog,
FreeRTOS software timers, trace facilities, long FAT names and the per-file
FatFs cache; it limits FatFs to one volume and VFS to three registrations.
UART0 at 115200 remains enabled for diagnostics. The single-core configuration
does not reduce decoder
parallelism (HLV decoding is sequential); it frees the second cache bank and
allows the otherwise unused 8 KiB RTC Fast RAM to join the byte-addressable
heap. Slow exception-emulated byte access to ordinary IRAM stays disabled.

## Build and flash

All generated dependencies are placed below this directory in `.tools`:

```powershell
.\setup.ps1
.\build.ps1
.\flash.ps1 -Port COM8
.\monitor.ps1 -Port COM8
```

The repository-level wrappers run the same commands:

```powershell
.\setup.ps1
.\scripts\build_esp32.ps1
.\scripts\upload_esp32.ps1 -Port COM8
```

The board normally needs manual download mode: hold `BOOT`, briefly press and
release `RST`, then release `BOOT`. The project selects esptool's `no_reset`
connection mode so it does not undo that sequence before connecting.

Place `video.hlv` in the root of a FAT16/FAT32 microSD card. The file itself is
not written to internal flash.

## Resource choices

- Display: ST7789 at 40 MHz, two reusable 320x4 RGB565 DMA strips.
- Storage: SDSPI at 20 MHz, eight reusable 7680-byte packet blocks (60 KiB).
- Video: two packed Y6/U5/V5 4:2:0 frames plus a macroblock-row work area;
  138,240 bytes at 320x180 instead of 184,320 bytes for two 8-bit frames.
- Audio: 4 KiB stream buffer feeding six 256-byte DAC DMA descriptors.
- Flash: one 1.5 MiB factory application partition; no NVS or OTA partition.

Changing `kScaleVideoToDisplay` in `main/player_settings.hpp` selects native
centred presentation or nearest-neighbour scaling to 320x240.
`kUseCompactY6U5V5` selects the compact decoder and is `true` in the current
test build. Set it to `false` to restore bit-exact 8-bit YUV420 references.
