# HLV-1 player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB. It normally uses an
ST7789 display controller and an NS8002/8002A-class mono amplifier driven by
DAC GPIO26.

## What the firmware does

- reads `/sdcard/video.hlv` from a FAT16/FAT32 microSD card;
- decodes HLV-1 stream versions 1 through 13;
- plays 320x180 Big Buck Bunny centred on the 320x240 panel without scaling;
- converts YUV420 to RGB565 in 16-row strips, without a full RGB framebuffer;
- reads SPI3/VSPI at 40 MHz with DMA into a 16 KiB aligned stdio read-ahead
  buffer, then fills nine reusable 7680-byte packet blocks (67.5 KiB total);
- writes the ST7789 on the independent SPI2/HSPI bus using two alternating
  320x16 DMA strips, overlapping conversion with transfer;
- decodes frame N on CPU1 while CPU0 converts and queues frame N-1 for the
  display, without copying either compressed packets or YUV frames;
- plays unsigned 8-bit mono PCM through the ESP32 DAC and onboard amplifier;
- feeds six 256-sample DMA buffers from a separate 4 KiB FreeRTOS stream
  buffer, so display transfers do not directly clock the sound;
- repeats the file continuously;
- prints decode/render timing, audio underruns and free heap to the 460800-baud
  serial console.

The test build enables packed Y6/U5/V5 4:2:0 frame storage. At 320x180 the two
packed frames and the decoder's macroblock-row work area consume 138,240 bytes,
instead of 184,320 bytes for two padded 8-bit frames. The 320x180 profile pads
internally to 320x192, preserves the official movie resolution and leaves 30
black rows above and below the picture.

The compile-time flag `kUseCompactY6U5V5` in
`firmware/esp32_2432s028_hlv_player_idf/main/player_settings.hpp` is currently
`true`. Ordinary v1-v12 predictors remain 8-bit in the bitstream. A v13
`LITERAL` macroblock is stored directly as packed Y6/U5/V5, while other modes
are rounded to that precision when committed to the compact reference. This
saves 46,080 bytes at 320x180, but the compact treatment of non-literal modes
is intentionally not bit-exact: banding and gradual P-frame prediction drift
are possible. Set the flag to `false` for the original 8-bit reference path.
The compact path expands consecutive reference spans and display rows in
batches; literal blocks bypass the temporary 8-bit macroblock completely.
The application and decoder components are compiled with `-O3`.

The current build sets `kEnableAudio = true` in the same settings file. Its
4 KiB FreeRTOS audio stream is statically allocated, while DAC descriptors and
the audio task are created only after the large decoder frames and packet pool.
Periodic logs report queued audio bytes and underruns so starvation can be
distinguished from a DAC failure or reset.

Playback timing comes from `fps_num/fps_den` in the HLV sequence header. With
audio, the exact rational frame index is converted to a target PCM sample
position and the DAC sample counter is the master clock. Without audio, the
ESP timer advances by the quotient and remainder of
`1,000,000 * fps_den / fps_num`, so fractional rates do not accumulate
microsecond-rounding drift. The current `kDropLateVideoFrames` mode preserves
that media time: every predictive frame is decoded, but a frame that is already
more than one file-defined interval late is not sent to the display. Audio
samples are never dropped.

This was verified on the physical ESP32 with the same firmware binary. A
`24/1` HLV test measured 23.985 decoded frames/s over 200 frames; the normal
`15/1` file measured 14.984 frames/s. Both runs had zero decode-sequence gaps,
audio rebuffers, missing samples and silence chunks.

For a strict hardware check, the firmware emits a `V,...` record containing
the file dimensions and rational frame rate, plus an `A,...` audio record every
30 frames. The project-local collector calculates the frame budget from `V`
and rejects decode-sequence gaps, rebuffers and missing samples:

```powershell
.\capture-player-metrics.ps1 -Port COM8 -Frames 900 -TimeoutSeconds 120
```

The retained build completed this test with 900 consecutive frames,
927,232 played samples and zero rebuffers, underrun samples or silence DMA
chunks.

## Dual-core playback mode

`kUseDualCorePipeline` in `main/player_settings.hpp` is enabled in the current
build. The main task remains pinned to PRO CPU (CPU0) and owns SD reads, RGB565
conversion and display DMA. A 4 KiB worker task pinned to APP CPU (CPU1)
performs ordered HLV decoding. Two one-entry FreeRTOS queues pass a packet
descriptor to CPU1 and return a frame descriptor to CPU0; pixel data remains
in the decoder's two existing predictive frame buffers.

HLV P-frames depend on the immediately preceding reconstructed frame and their
entropy stream is sequential, so two arbitrary frames cannot be safely decoded
at the same time. Instead, while CPU1 decodes frame N from frame N-1, CPU0 only
reads frame N-1 for display conversion. Before frame N+1 starts, both actions
have completed, allowing the old buffer to be reused safely. This overlaps the
two largest CPU stages while preserving bitstream order and adding no third
framebuffer. Set the flag to `false` to retain the sequential comparison mode.

## Segmented ESP32 decoder

The firmware uses the separate `HlvEsp32Decoder` front end. It creates the
portable predictive decoder first and then allocates a nine-block packet
pool from internal SRAM. Every block preferentially uses DMA-capable memory;
if decoder fragmentation exhausts that heap, only the remaining blocks fall
back to ordinary 8-bit internal SRAM. Its 69,120-byte capacity covers a fully
literal 320x180 Y6/U5/V5 key frame plus one 16 kHz mono audio interval without
requiring one equally large contiguous heap region.

Packet data is read sequentially into the blocks and CRC-32 is updated during
the read. The bit reader advances to the next block without joining or copying
the payload. PCM at the packet tail is likewise sent to the FreeRTOS audio
stream one contiguous span at a time. The blocks are retained for the complete
playback session, so the frame loop performs no packet `malloc` or `free`.
The startup log reports the actual DMA-capable block count; the ESP-IDF SD
driver supplies its DMA-safe fallback for any ordinary internal block.

The stdio layer uses a fixed 16 KiB aligned read-ahead buffer. This costs 16
KiB of the RAM saved by compact frame storage, but combines small packet/header
reads into longer SDSPI transactions. On the reference card it reduced average
packet-read time from roughly 50--55 ms to 5--6 ms.

The pool capacity is 69,120 bytes. A packet larger than that is rejected with
an out-of-memory error instead of fragmenting the ESP32 heap. The ninth block
uses 7,680 additional bytes compared with the v12 player and leaves roughly
28 KiB free in the current 320x180 compact memory-budget estimate; confirm the
actual minimum heap from the serial log on physical hardware.

The recommended audio profile is `PCM_U8`, mono, 16 kHz. It adds 160 KB to a
ten-second file. The DAC DMA clock uses APLL rather than frame timing, while
audio samples are divided among frame packets with rational accounting to
avoid cumulative A/V drift. See [`AUDIO_FORMAT.md`](AUDIO_FORMAT.md) for the
container layout. DAC writes use a finite timeout so stopping or reopening a
video cannot strand an audio task and leak its stack.

## Display scaling setting

The compile-time flag `kScaleVideoToDisplay` is in
`firmware/esp32_2432s028_hlv_player_idf/main/player_settings.hpp`:

```cpp
constexpr bool kScaleVideoToDisplay = false;
```

- `false` draws the video at its native resolution in the centre. The complete
  display is cleared once, then only the video rectangle is transferred over
  SPI on each frame. A 320x180 frame is placed at (0, 30), leaving black
  borders only above and below it.
- `true` stretches each frame to 320x240 using nearest-neighbour sampling and
  transfers the complete display on every frame.

Both modes convert and send 16 rows at a time and do not allocate a full
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

## Pure ESP-IDF firmware and project-local dependencies

The default firmware is the independent project in
`firmware/esp32_2432s028_hlv_player_idf`. It uses ESP-IDF APIs directly:
`esp_lcd` for the ST7789, SDSPI/FatFs for the card, and the continuous DAC
driver for sound. Arduino and LovyanGFX are not linked. The previous Arduino
sketch remains next to it only as a migration reference.

The firmware toolchain is isolated from every other project. Its setup script
downloads the pinned full ESP-IDF 5.5.5 archive, a local Python and the ESP32
compiler/debug tools into the firmware's own `.tools` directory:

```powershell
.\setup.ps1
```

FFmpeg used by the desktop converter remains in the repository's
`local_tools\ffmpeg` directory. Firmware dependencies are ignored by Git
because they are generated and large. No global ESP-IDF, Arduino package or
Python installation is used by the firmware build.

Build the SD-player firmware:

```powershell
.\scripts\build_esp32.ps1
```

Connect the board through its CH340 USB port, find the port with
Windows Device Manager, and upload:

```powershell
.\scripts\upload_esp32.ps1 -Port COM8
```

Replace `COM8` if the board appears on another port. After the EN-to-GND
capacitor modification, the uploader uses the tested CH340C DTR/RTS sequence
and requires no button presses: reset is held for 500 ms and GPIO0 for 50 ms.
It writes the bootloader, partition table and application to internal flash.
The uploader defaults to a conservative 460800 baud. `video.hlv` is not
flashed; it remains on the removable card.

With the normal player running, an HLV file can instead be written into the
card's `/HLV` directory without removing the card:

```powershell
.\upload-video.ps1 -Port COM8 -File ..\..\out\video.hlv
```

The command handshake remains at 460800 baud; the verified data-transfer
default is 2 Mbaud. The player stops video and audio, allocates one temporary
60 KiB receive block, shows a progress bar, and verifies per-block and
whole-file CRC32 before replacing the target. On this CH340C board an 8 MiB
transfer sustained 111.3 KiB/s. A 2.5 Mbaud experiment timed out, so 2 Mbaud is
the maximum retained rate; 1.5 Mbaud, 921600 and 460800 are available as
fallbacks.

The IDF driver defaults to an 80 MHz LCD clock. If the display
still shows unstable pixels, lower `kDisplayClockHz` in
`main/player_settings.hpp` to 26 MHz.

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
- <https://github.com/rzeldent/platformio-espressif32-sunton>
- <https://github.com/rzeldent/esp32-smartdisplay>
- <https://github.com/lovyan03/LovyanGFX>

Local board documentation and the CH340C automatic-BOOT investigation are in
[`docs/board`](board/README.md).
