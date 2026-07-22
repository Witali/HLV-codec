# HLV-1 player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB. It normally uses an
ST7789 display controller and an NS8002/8002A-class mono amplifier driven by
DAC GPIO26.

## What the firmware does

- reads `/video.hlv` from the internal LittleFS flash partition;
- decodes HLV-1 stream versions 1 through 12;
- plays the first 256x192 profile centred on the 320x240 panel without scaling;
- converts YUV420 to RGB565 in eight-row strips, without a full RGB framebuffer;
- plays unsigned 8-bit mono PCM through the ESP32 DAC and onboard amplifier;
- feeds six 256-sample DMA buffers from a separate 8 KiB FreeRTOS stream
  buffer, so display transfers do not directly clock the sound;
- repeats the file continuously;
- prints decode/render timing, audio underruns and free heap to the 115200-baud
  serial console.

The HLV decoder keeps two padded YUV420 frames.  Full 320x240 would consume
230,400 bytes and exceed the largest usable internal-DRAM regions on this
board.  The first profile uses 256x192 at 15 fps: its two frames consume
147,456 bytes and output is shown pixel-for-pixel with black borders.

The recommended audio profile is `PCM_U8`, mono, 16 kHz. It adds 160 KB to a
ten-second file. The DAC DMA clock uses APLL rather than frame timing, while
audio samples are divided among frame packets with rational accounting to
avoid cumulative A/V drift. See [`AUDIO_FORMAT.md`](AUDIO_FORMAT.md) for the
container layout.

## Prepare a video on Windows

Visual Studio C/C++ tools must be installed.  FFmpeg is downloaded into the
project automatically.  To generate a ten-second built-in test pattern:

```powershell
.\scripts\prepare_esp32_video.ps1
```

To convert an existing video:

```powershell
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4
```

The first audio stream is automatically downmixed to mono, resampled to
16 kHz and muxed into the HLV file. The default volume is deliberately 20%
because the onboard amplifier can be loud. Conversion controls include:

```powershell
# Quieter 16 kHz audio
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -AudioVolume 0.10

# Video only
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -NoAudio
```

The result is `out\video.hlv`. The build script packages it as
`build\esp32\littlefs.bin`. The default 4 MB partition layout reserves
0x160000 bytes (about 1.38 MiB) for LittleFS. Always check the resulting size:
uncompressed 16 kHz audio consumes 16,000 bytes per second in addition to the
compressed video.

## Project-local dependencies

The toolchain is isolated from every other project.  The bootstrap script
downloads pinned versions of FFmpeg, Arduino CLI, the Espressif core and
LovyanGFX into this repository only:

```powershell
.\scripts\bootstrap.ps1
```

Dependencies are placed in `tools\ffmpeg`, `tools\arduino-cli` and
`local_tools\arduino`.  These directories are ignored by Git because they
are generated and large.  Only PowerShell and the Visual Studio C compiler
are shared system prerequisites; no global Arduino or FFmpeg files are used.

Build the firmware and the LittleFS image containing `video.hlv`:

```powershell
.\scripts\build_esp32.ps1
```

Connect the board through its CH340 USB port, find the port with
the local CLI, and upload:

```powershell
.\scripts\arduino.ps1 board list
.\scripts\upload_esp32.ps1 -Port COM8
```

Replace `COM8` if the board appears on another port.  This CYD2USB revision
does not reliably enter the ROM downloader through the CH340 control lines.
When the script asks, hold the board's `BOOT` button, briefly press and release
`RST`, then release `BOOT`.  The script waits for that sequence and directly
writes the bootloader, partition table, program and `littlefs.bin` to internal
flash; no microSD card is needed.

The uploader defaults to a conservative 460800 baud. If a transfer is
interrupted only while writing the large LittleFS image after the application
has already passed hash verification, retry just that image:

```powershell
.\scripts\upload_esp32.ps1 -Port COM8 -SkipBuild -LittleFSOnly
```

If the display shows unstable pixels, lower `cfg.freq_write` from 80 MHz to
40 MHz in `LGFX_CYD2USB.hpp`.

## Hardware mapping

| Device | GPIOs |
| --- | --- |
| ST7789 TFT (HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21 |
| unused microSD (VSPI) | SCK 18, MOSI 23, MISO 19, CS 5 |
| onboard amplifier | DAC GPIO26 |

The speaker connector is a bridged amplifier output. Connect a small speaker
between the connector's two output pins; do not connect either speaker pin to
board ground and do not feed that connector into a grounded oscilloscope or
PC line input. GPIO26 itself carries the DAC signal before amplification.

References:

- <https://github.com/espressif/arduino-esp32/tree/master/variants/jczn_2432s028r>
- <https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display>
- <https://github.com/lovyan03/LovyanGFX>
