# HLV-1 player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB. It normally uses an
ST7789 display controller and an NS8002/8002A-class mono amplifier driven by
DAC GPIO26.

## What the firmware does

- reads `/sdcard/video.hlv` from a FAT16/FAT32 microSD card;
- decodes HLV-1 stream versions 1 through 12;
- plays 320x180 Big Buck Bunny centred on the 320x240 panel without scaling;
- converts YUV420 to RGB565 in four-row strips, without a full RGB framebuffer;
- reads packet payloads on SPI3/VSPI at 20 MHz directly into eight reusable,
  DMA-capable 7680-byte blocks (60 KiB total);
- writes the ST7789 on the independent SPI2/HSPI bus using two alternating
  320x4 DMA strips, overlapping conversion with transfer;
- plays unsigned 8-bit mono PCM through the ESP32 DAC and onboard amplifier;
- feeds six 256-sample DMA buffers from a separate 4 KiB FreeRTOS stream
  buffer, so display transfers do not directly clock the sound;
- repeats the file continuously;
- prints decode/render timing, audio underruns and free heap to the 115200-baud
  serial console.

The test build enables packed Y6/U5/V5 4:2:0 frame storage. At 320x180 the two
packed frames and the decoder's macroblock-row work area consume 138,240 bytes,
instead of 184,320 bytes for two padded 8-bit frames. The 320x180 profile pads
internally to 320x192, preserves the official movie resolution and leaves 30
black rows above and below the picture.

The compile-time flag `kUseCompactY6U5V5` in
`firmware/esp32_2432s028_hlv_player_idf/main/player_settings.hpp` is currently
`true`. The on-disk HLV stream remains ordinary 8-bit YUV420. The compact
decoder rounds reconstructed luma to six bits and chroma to five bits after
each macroblock, then packs both predictive frames by row. This saves 46,080
bytes at 320x180, but it is intentionally not bit-exact: banding and gradual
P-frame prediction drift are possible. Set the flag to `false` for the original
8-bit reference path. The compact path expands consecutive reference spans and
display rows in batches rather than performing a fresh bit lookup per pixel;
the application and decoder components are compiled with `-O3`.

The current diagnostic build also sets `kEnableAudio = false` in the same
settings file. This bypasses DAC setup and the FreeRTOS audio queue while
leaving the audio track in the HLV file untouched, allowing compact video
playback to be tested independently of the DAC DMA restart fault.

## Segmented ESP32 decoder

The firmware uses the separate `HlvEsp32Decoder` front end. It creates the
portable predictive decoder first and then allocates an eight-block packet
pool from internal SRAM. Every block preferentially uses DMA-capable memory;
if decoder fragmentation exhausts that heap, only the remaining blocks fall
back to ordinary 8-bit internal SRAM. Its 65,536-byte capacity covers the
60,538-byte maximum packet in the prepared Big Buck Bunny file without
requiring one equally large contiguous heap region.

Packet data is read sequentially into the blocks and CRC-32 is updated during
the read. The bit reader advances to the next block without joining or copying
the payload. PCM at the packet tail is likewise sent to the FreeRTOS audio
stream one contiguous span at a time. The blocks are retained for the complete
playback session, so the frame loop performs no packet `malloc` or `free`.
The startup log reports the actual direct-DMA block count; the ESP-IDF SD
driver supplies its DMA-safe fallback for any ordinary internal block.

The pool capacity is 61,440 bytes, which covers the measured 60,538-byte
maximum packet in the prepared movie while saving 4 KiB versus 8x8 KiB. The
ordinary desktop encoder and decoder retain their contiguous packet path.
The on-disk HLV format is unchanged. A packet larger than 61,440 bytes is
rejected with an out-of-memory error instead of fragmenting the ESP32 heap.

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

Both modes convert and send four rows at a time and do not allocate a full
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
`tools\ffmpeg` directory. Firmware dependencies are ignored by Git because
they are generated and large. No global ESP-IDF, Arduino package or Python
installation is used by the firmware build.

Build the SD-player firmware:

```powershell
.\scripts\build_esp32.ps1
```

Connect the board through its CH340 USB port, find the port with
Windows Device Manager, and upload:

```powershell
.\scripts\upload_esp32.ps1 -Port COM8
```

Replace `COM8` if the board appears on another port.  This CYD2USB revision
does not reliably enter the ROM downloader through the CH340 control lines.
When the script asks, hold the board's `BOOT` button, briefly press and release
`RST`, then release `BOOT`.  The script waits for that sequence and directly
writes the bootloader, partition table and application to internal flash. The
uploader defaults to a conservative 460800 baud. `video.hlv` is not flashed;
it remains on the removable card.

The IDF driver defaults to a conservative 40 MHz LCD clock. If the display
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
- <https://github.com/lovyan03/LovyanGFX>
