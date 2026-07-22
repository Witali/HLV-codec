# HLV-1 player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB.  It normally uses an
ST7789 display controller.  The player is deliberately video-only for the
first hardware milestone.

## What the firmware does

- reads `/video.hlv` from the internal LittleFS flash partition;
- decodes HLV-1 stream versions 1 through 12;
- accepts images up to 320x240;
- converts YUV420 to RGB565 in eight-row strips, without a full RGB framebuffer;
- repeats the file continuously;
- prints decode/render timing and free heap to the 115200-baud serial console.

The HLV decoder keeps two padded YUV420 frames.  At 320x240 this consumes
230,400 bytes, so the starter conversion uses 15 fps and moderate quality to
leave room for compressed packets and the Arduino runtime.

Audio is not implemented yet.  The board amplifier is connected to GPIO26 and
can be added after video playback is stable.

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

The result is `out\video.hlv`.  The build script packages it as
`build\esp32\littlefs.bin`.  The default 4 MB partition layout reserves
0x160000 bytes (about 1.38 MiB) for LittleFS, so the generated 991 KB test
video fits with room for filesystem metadata.

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
.\scripts\upload_esp32.ps1 -Port COM5
```

Replace `COM5` with the detected port.  The upload script writes both the
program and `littlefs.bin` to internal flash; no microSD card is needed.  If
the display shows unstable pixels, lower `cfg.freq_write` from 80 MHz to
40 MHz in `LGFX_CYD2USB.hpp`.

If upload reports `Wrong boot mode (0x13)`, enter the ROM downloader manually:
hold the board's `BOOT` button, briefly press and release `RST`, then release
`BOOT` and run the upload command again.

## Hardware mapping

| Device | GPIOs |
| --- | --- |
| ST7789 TFT (HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21 |
| unused microSD (VSPI) | SCK 18, MOSI 23, MISO 19, CS 5 |
| onboard amplifier | DAC GPIO26 |

References:

- <https://github.com/espressif/arduino-esp32/tree/master/variants/jczn_2432s028r>
- <https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display>
- <https://github.com/lovyan03/LovyanGFX>
