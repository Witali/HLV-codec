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
UART0 at 115200 remains enabled for diagnostics. The default dual-core
pipeline pins ordered HLV decoding to APP CPU (CPU1), while the main task on
PRO CPU (CPU0) converts the preceding frame to RGB565 and queues its SPI DMA
strips. Predictive P-frames are never decoded out of order. Dual-core mode
cannot add the 8 KiB RTC Fast RAM to the heap. Slow exception-emulated byte
access to ordinary IRAM stays disabled.
ESP-IDF libraries retain size optimization, while the latency-sensitive
`main` and `hlv1` components explicitly use `-O3`.

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

The ST7789 and SD-card SPI clocks are available under the `HLV player`
section of ESP-IDF menuconfig:

```powershell
.\idf.ps1 -IdfArguments @("menuconfig")
```

Clean builds default to an 80 MHz display clock and a 40 MHz SD-card clock.
Reduce either value if the board or card shows transfer errors.

Place `video.hlv` in the root of a FAT16/FAT32 microSD card. The file itself is
not written to internal flash.

## Xtensa QEMU decoder benchmark

An off-board benchmark boots the real ESP-IDF decoder in Espressif QEMU and
measures only `decode()` with the guest ESP32 cycle counter. Its 120-frame test
clip contains four complete GOP windows distributed across the existing HLV
file. Packets are copied byte-for-byte and every window starts at a keyframe;
the benchmark never runs or modifies the encoder.

```powershell
.\qemu-benchmark.ps1 -BitReaderBits 32
.\qemu-benchmark.ps1 -BitReaderBits 64
```

The first run installs QEMU under this project's `.tools` directory. Generated
clips and QEMU builds are excluded from Git. Guest cycle ratios are useful for
32-bit Xtensa A/B comparisons, but absolute playback speed still requires the
physical board because QEMU is not cycle-accurate and does not model SD or
display DMA timing.

## Resource choices

- Display: ST7789 at configurable 80 MHz, two reusable 320x16 RGB565 DMA
  strips.
- Storage: SDSPI DMA at configurable 40 MHz, a 16 KiB aligned read-ahead
  buffer and nine reusable 7680-byte packet blocks (67.5 KiB), enough for a
  fully literal 320x180 key frame plus one mono audio interval.
- Video: two packed Y6/U5/V5 4:2:0 frames plus a macroblock-row work area;
  138,240 bytes at 320x180 instead of 184,320 bytes for two 8-bit frames.
  Stream v13 literal blocks are copied directly into this packed storage.
- Scheduling: one 4 KiB CPU1 decoder task, one 3 KiB high-priority CPU0 audio
  reader and two one-entry decode queues. Only frame descriptors cross cores,
  so no YUV frame or packet payload is copied.
- Audio: a static 4 KiB stream buffer feeding a permanent ring of six
  256-sample DAC DMA descriptors directly from the completion ISR. A second
  sequential file cursor skips compressed video and prefetches only PCM packet
  tails. The DAC sample count is the master video clock. The current
  frame-preserving mode cyclically replays the existing 96 ms DMA ring without
  consuming queued PCM while video is late.
- Flash: one 1.5 MiB factory application partition; no NVS or OTA partition.

Changing `kScaleVideoToDisplay` in `main/player_settings.hpp` selects native
centred presentation or nearest-neighbour scaling to 320x240.
`kUseCompactY6U5V5` selects the compact decoder and is `true` in the current
test build. Set it to `false` to restore bit-exact 8-bit YUV420 references.
`kUseDualCorePipeline` selects the CPU1-decode/CPU0-render pipeline and is also
`true`; set it to `false` to compare against sequential playback without
changing the HLV file.
`kEnableAudio` enables PCM_U8 playback through DAC GPIO26. The current test
build sets it to `true`; the 4 KiB FreeRTOS stream buffer is statically
allocated and starts with a 3 KiB preroll. Files without audio, an explicitly
disabled output, or a failed audio reader/DAC automatically use the monotonic
ESP timer as the video clock. The periodic log reports queued bytes, controlled
rebuffer events, silence DMA chunks and cyclic-repeat activity.
`kAvSyncMode` selects between `kDropLateVideoFrames`, which keeps audio
continuous and omits late display transfers, and `kLoopAudioForLateVideo`,
which presents every frame and repeats the six already allocated DMA
descriptors while video catches up. The latter is enabled in the current test
build and does not allocate additional frame or audio buffers.
