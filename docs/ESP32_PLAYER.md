# HLV-1 player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB. It normally uses an
ST7789 display controller and an NS8002/8002A-class mono amplifier driven by
DAC GPIO26.

## What the firmware does

- reads `/sdcard/video.hlv` from a FAT16/FAT32 microSD card;
- decodes HLV-1 stream versions 1 through 12;
- plays 320x180 Big Buck Bunny centred on the 320x240 panel without scaling;
- converts YUV420 to RGB565 in eight-row strips, without a full RGB framebuffer;
- reads SD sectors on SPI3/VSPI at 20 MHz using DMA and an aligned 8 KiB
  read-ahead buffer;
- writes the ST7789 on the independent SPI2/HSPI bus using DMA;
- plays unsigned 8-bit mono PCM through the ESP32 DAC and onboard amplifier;
- feeds six 256-sample DMA buffers from a separate 8 KiB FreeRTOS stream
  buffer, so display transfers do not directly clock the sound;
- repeats the file continuously;
- prints decode/render timing, audio underruns and free heap to the 115200-baud
  serial console.

The HLV decoder keeps two padded YUV420 frames. Full 320x240 would consume
230,400 bytes and exceed the practical internal-RAM budget once SD and audio
DMA are active. The 320x180 profile pads internally to 320x192; its two frames
consume 184,320 bytes. It preserves the official movie resolution and leaves
30 black rows above and below the picture.

The recommended audio profile is `PCM_U8`, mono, 16 kHz. It adds 160 KB to a
ten-second file. The DAC DMA clock uses APLL rather than frame timing, while
audio samples are divided among frame packets with rational accounting to
avoid cumulative A/V drift. See [`AUDIO_FORMAT.md`](AUDIO_FORMAT.md) for the
container layout.

## Display scaling setting

The compile-time flag `kScaleVideoToDisplay` is in
`firmware/esp32_2432s028_hlv_player/PlayerSettings.hpp`:

```cpp
constexpr bool kScaleVideoToDisplay = false;
```

- `false` draws the video at its native resolution in the centre. The complete
  display is cleared once, then only the video rectangle is transferred over
  SPI on each frame. A 320x180 frame is placed at (0, 30), leaving black
  borders only above and below it.
- `true` stretches each frame to 320x240 using nearest-neighbour sampling and
  transfers the complete display on every frame.

Both modes convert and send eight rows at a time and do not allocate a full
RGB framebuffer. The scaling mode adds only coordinate maps and one cached
RGB row (1760 bytes in the current build).

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
16 kHz and muxed into the HLV file. A mild compressor defaults to -18 dB,
2.5:1, 20 ms attack, 250 ms release and +2 dB makeup before the deliberately
low 20% output level. This makes dialogue more audible without harsh peaks on
the onboard amplifier. Conversion controls include:

```powershell
# Quieter 16 kHz audio
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -AudioVolume 0.10

# Video only
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -NoAudio

# Disable dynamic-range compression
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 `
    -NoAudioCompression
```

The result is `out\video.hlv`. Copy it to the root of a FAT16/FAT32 card under
the exact name `video.hlv`. Uncompressed 16 kHz audio consumes 16,000 bytes per
second in addition to the compressed video. The optional helper copies and
verifies the file when the card is mounted as a Windows drive:

```powershell
.\scripts\copy_video_to_sd.ps1 -DestinationRoot E:\
```

### Big Buck Bunny example

Download and verify the official Blender Foundation 320x180 MP4, then retain
its native resolution and complete audio track:

```powershell
.\scripts\fetch_big_buck_bunny.ps1
.\scripts\prepare_esp32_video.ps1 `
    -InputFile .\out\sources\BigBuckBunny_320x180.mp4 `
    -OutputFile .\out\video.hlv -Width 320 -Height 180
```

The generated reference file has 8,947 frames at 15 fps (596.47 seconds),
9,543,466 audio samples and a total size of 68,785,991 bytes. The downloaded
MP4 and generated HLV are ignored by Git. Big Buck Bunny is a Blender
Foundation open movie licensed under Creative Commons Attribution 3.0:
<https://studio.blender.org/projects/big-buck-bunny/>.

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

Build the SD-player firmware:

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
writes the bootloader, partition table and application to internal flash. The
uploader defaults to a conservative 460800 baud. `video.hlv` is not flashed;
it remains on the removable card.

If the display shows unstable pixels, lower `cfg.freq_write` from 80 MHz to
40 MHz in `LGFX_CYD2USB.hpp`.

## Hardware mapping

| Device | GPIOs |
| --- | --- |
| ST7789 TFT (HSPI) | SCK 14, MOSI 13, MISO 12, CS 15, DC 2, BL 21 |
| microSD (SPI3/VSPI DMA) | SCK 18, MOSI 23, MISO 19, CS 5 |
| onboard amplifier | DAC GPIO26 |

The speaker connector is a bridged amplifier output. Connect a small speaker
between the connector's two output pins; do not connect either speaker pin to
board ground and do not feed that connector into a grounded oscilloscope or
PC line input. GPIO26 itself carries the DAC signal before amplification.

References:

- <https://github.com/espressif/arduino-esp32/tree/master/variants/jczn_2432s028r>
- <https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display>
- <https://github.com/lovyan03/LovyanGFX>
