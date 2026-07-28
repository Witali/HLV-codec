# Multi-codec player for ESP32-2432S028 CYD2USB

The board in `IMG_20260722_174354.jpg` and `IMG_20260722_174401.jpg` is the
two-USB ESP32-2432S028 variant commonly called CYD2USB. It normally uses an
ST7789 display controller and an NS8002/8002A-class mono amplifier driven by
DAC GPIO26.

The staged rewrite of this firmware from C++ to strict C99, including its
preserved baseline and all-codec physical A/B matrix, is tracked in
[`ESP32_C99_MIGRATION.md`](ESP32_C99_MIGRATION.md).

## What the firmware does

- reads the selected filename from `/sdcard/HLV/play.txt`;
- lets the physical BOOT button browse `/sdcard/HLV`: a short press opens the
  browser or advances to the next video, while an 800 ms hold saves the
  displayed filename to `play.txt` and starts playback;
- decodes stable standalone HLV-1 v14, standard AVI/MJPEG, DivX 3
  (`DIV3`/`MP43`) AVI up to 320x240 with compact Y6/U5/V5 references, BPV1
  v1 through v7 including adaptive RAW records, active per-GOP palettes and
  experimental v7 pixel motion,
  the constrained MPEG-1 Video/MP2 profile up to 320x240, and baseline
  H.263/intra-only H.263+ with optional AMR-NB mono audio in 3GP or PCM S16LE
  mono audio in AVI at `176x144`, `256x144`, `256x192`, `320x180`,
  `320x240`, or `352x288` CIF; the ESP32 copies CIF's central `320x240`
  square-pixel coded area to the panel pixel-for-pixel without scaling;
- shows `NO SELECTED FILE.` on the display instead of guessing a fallback
  when `play.txt` is absent;
- plays 320x180 Big Buck Bunny centred on the 320x240 panel without scaling;
- converts HLV/MPEG YUV420, MJPEG MCU rows or BPV palette blocks to RGB565 in
  bounded strips (16 rows normally, 8 rows for compact H.263 playback),
  without a full RGB framebuffer;
- reads SPI3/VSPI at 40 MHz with DMA into a dynamically allocated aligned
  stdio read-ahead buffer (4 KiB for MPEG-1/DivX 3/H.263/BPV v7 and 16 KiB otherwise); HLV then
  streams each packet through one reusable 7,680-byte refill buffer, while MJPEG
  and BPV v1-v6 use bounded maximum-frame packet buffers; BPV v7 instead uses
  a fixed 16 KiB CPU1 producer ring, a 4 KiB decoder refill buffer and its
  mode map;
- writes the ST7789 on the independent SPI2/HSPI bus using DMA strips.
  DivX 3 uses one 320x16 allocation; H.263 divides one such allocation into
  two 320x8 strips. Other codecs use two 320x16 allocations;
- decodes HLV, BPV v1-v6 or MPEG-1 frame N on CPU1 while CPU0 converts and queues
  frame N-1 for the display, without copying compressed packets or frame
  payloads;
- plays unsigned 8-bit mono PCM through the ESP32 DAC and onboard amplifier;
- feeds six 256-sample DMA buffers from a separate 4 KiB FreeRTOS stream
  buffer, so display transfers do not directly clock the sound;
- repeats the file continuously;
- prints decode/render timing, audio underruns and free heap to the 460800-baud
  serial console.

The file browser sorts supported regular files case-insensitively and wraps
after the last one. It recognizes `.hlv`, `.bpv1`, `.avi`, `.mpg`, `.mpeg`,
`.3gp` and `.3gpp`. The directory is rescanned for each short press, so the
firmware does not allocate a filename array proportional to the number of
files on the card. BOOT remains the ESP32 strap button: holding it during a
reset enters the ROM downloader.

The test build enables packed Y7/U6/V6 4:2:0 frame storage. At 320x180 the two
packed frames, Q4 maps and decoder macroblock-row work area consume 164,160 bytes,
instead of 184,320 bytes for two padded 8-bit frames. The 320x180 profile pads
internally to 320x192, preserves the official movie resolution and leaves 30
black rows above and below the picture.

The compile-time flag `PLAYER_USE_COMPACT_HLV_REFERENCE` in
`firmware/esp32_2432s028_hlv_player_idf_c/main/player_settings.h` is currently
`1`. Stable v14 makes Y7/U6/V6 plus a separate signed Q4 local-average
coefficient for every 8x8 Y, U and V block normative. A `LITERAL` macroblock
carries four Y coefficients plus one U and one V coefficient. The packed and
expanded decoders reconstruct identical samples, preventing coherent
prediction drift. Set the flag to `0` for the expanded validation path.
The compact path expands consecutive reference spans and display rows in
batches; literal blocks bypass the temporary 8-bit macroblock completely.
The application and decoder components are compiled with `-O3`.

The current build sets `kEnableAudio = true` in the same settings file. Its
4 KiB FreeRTOS audio stream is statically allocated, while DAC descriptors and
the audio task are created only after the large decoder frames and stream buffer.
Periodic logs report queued audio bytes and underruns so starvation can be
distinguished from a DAC failure or reset.

The MPEG-1 decoder has a separate compact path. It keeps two packed
Y6/U5/V5 reference frames, uses one 8-bit macroblock row for reconstruction,
and streams a compressed picture through a bounded 4 KiB elementary buffer.
At 320x240 those frame allocations total 170,880 bytes. The six packed planes
are allocated separately to avoid a single large contiguous-heap
requirement.

Playback timing comes from `fps_num/fps_den` in the active format header. With
HLV or MJPEG audio, the exact rational frame index is converted to a target PCM sample
position and the DAC sample counter is the master clock. Without audio, the
ESP timer advances by the quotient and remainder of
`1,000,000 * fps_den / fps_num`, so fractional rates do not accumulate
microsecond-rounding drift. The current `kDropThenLoopAudio` mode preserves
that media time: every predictive frame is decoded, up to two consecutive late
frames omit only their display transfer, then the existing DAC DMA ring is
repeated until video catches up. Repeated samples do not advance the media
clock, and the queued source audio resumes without dropped samples. The static
4 KiB audio queue uses a four-frame preroll target calculated from the file's
sample rate and rational frame rate.

The hybrid mode was verified on the physical ESP32 with the `320x180`, `24/1`
v13 test file. Two 900-frame runs measured 23.894 and 23.953 decoded frames/s.
The runs omitted 2 and 6 display transfers respectively and each entered one
short audio hold (9 and 4 DMA chunks), instead of allowing an unbounded visual
freeze. Both runs had zero decode-sequence gaps, audio rebuffers, missing
samples and silence chunks.

For a strict hardware check, the firmware emits a `V,...` record containing
the file dimensions and rational frame rate, plus an `A,...` audio record every
30 frames. The project-local collector calculates the frame budget from `V`
and rejects decode-sequence gaps, rebuffers and missing samples:

```powershell
.\capture-player-metrics.ps1 -Port COM8 -Frames 900 -TimeoutSeconds 120
```

The current hybrid build completed two consecutive invocations with 900 frame
records each and zero rebuffers, underrun samples or silence DMA chunks.

The same board, card and v13 file were also measured in two 900-frame runs at
each SD SPI clock. At 40 MHz, SD reads averaged 4.10 and 4.17 ms with p95 of
10.27 and 10.73 ms; at 20 MHz they averaged 5.58 and 5.65 ms with p95 of 12.81
and 12.96 ms. The 20 MHz runs also caused 13 total display skips and 4 audio
holds, versus 8 skips and 2 holds at 40 MHz. Neither clock lost audio samples,
but lowering the clock consistently reduced throughput without eliminating
latency spikes, so the retained default remains 40 MHz.

## Dual-core playback mode

`kUseDualCorePipeline` in `main/player_settings.hpp` is enabled in the current
build. The main task remains pinned to PRO CPU (CPU0) and normally owns SD
reads, RGB565 conversion and display DMA. A 4 KiB worker task pinned to APP CPU (CPU1)
performs ordered HLV, BPV or MPEG-1 decoding. Two one-entry FreeRTOS queues
pass a packet descriptor to CPU1 and return a frame descriptor to CPU0; pixel
data remains in the decoder's two existing predictive frame buffers.

HLV P-frames depend on the immediately preceding reconstructed frame and their
entropy stream is sequential, so two arbitrary frames cannot be safely decoded
at the same time. Instead, while CPU1 decodes frame N from frame N-1, CPU0 only
reads frame N-1 for display conversion. Before frame N+1 starts, both actions
have completed, allowing the old buffer to be reused safely. This overlaps the
two largest CPU stages while preserving bitstream order and adding no third
framebuffer. MPEG-1 uses the same schedule with two alternating packed
Y6/U5/V5 reference buffers; its copied frame descriptor remains valid while
CPU1 writes the other buffer. BPV uses its two block-record arrays in the same
way and finishes rendering the preceding frame before a v4 keyframe replaces
the active palette. Set the flag to `false` to retain the sequential
comparison mode.

BPV v7 uses CPU1 differently: a dedicated 4 KiB-stack producer is the only
task that reads its video cursor. It sends 4 KiB chunks into a fixed 16 KiB
FreeRTOS stream buffer while CPU0 decodes and submits RGB565 strips. The ring
is recreated on every open and destroyed before the file is closed.

## Streaming ESP32 decoder

The firmware uses the separate `HlvEsp32Decoder` front end. It creates the
portable predictive decoder first and then allocates one reusable 7,680-byte
refill buffer. The CPU1 decoder requests sequential spans directly from the
video file, updates CRC-32 as they arrive and drains the packet tail before
returning at the next frame header. Packet size is therefore not bounded by
heap, and the frame loop performs no packet `malloc` or `free`. The separate
audio cursor reads PCM tails independently.

The stdio layer uses a fixed 16 KiB aligned read-ahead buffer. This costs 16
KiB of the RAM saved by compact frame storage, but combines small packet/header
reads into longer SDSPI transactions. On the reference card it reduced average
packet-read time from roughly 50--55 ms to 5--6 ms.

The stream buffer replaces the former 69,120-byte packet pool. Together with
the 164,160-byte Y7/U6/V6+Q4 frame working set it reduces HLV's two dominant
allocations to 171,840 bytes, leaving packet size independent of RAM.

The recommended audio profile is `PCM_U8`, mono, 16 kHz. It adds 160 KB to a
ten-second file. The DAC DMA clock uses APLL rather than frame timing, while
audio samples are divided among frame packets with rational accounting to
avoid cumulative A/V drift. See [`AUDIO_FORMAT.md`](AUDIO_FORMAT.md) for the
container layout. DAC writes use a finite timeout so stopping or reopening a
video cannot strand an audio task and leak its stack.

## CIF pixel-exact crop

H.263 CIF scaling has been removed from the ESP32 player. Every `352x288` CIF
frame is cropped to its central `320x240` coded area: 16 columns are omitted
from each side and 24 rows from the top and bottom. The remaining pixels are
converted to RGB565 and copied to the panel one-for-one while preparing the
small DMA output strips. There are no nearest-neighbour or bilinear scaling
tables and no scaled framebuffer.

On the physical ESP32, the 30 fps CIF AVI test completed 300 frames at
29.993 fps with zero display skips, decode gaps, audio rebuffers, underrun
samples, or silence chunks.

`kScaleVideoToDisplay` still controls nearest-neighbour stretching for
non-CIF and non-H.263 playback. H.263 CIF deliberately ignores it.

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
16 kHz and muxed into the HLV file. The default audio path uses one smooth,
peak-detected level curve with a -20 dB threshold, 1.6:1 ratio, 0.01 ms attack,
250 ms release and a wide soft knee. A measurement pass calibrates the curve's
output so the actual source peak reaches -0.1 dBFS without a separate volume
filter or limiter. Quiet material is raised relative to loud material while
the full PCM range remains available. Conversion controls include:

```powershell
# Leave additional headroom
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -AudioPeakDb -1

# Video only
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 -NoAudio

# Downmix and resample without the level curve
.\scripts\prepare_esp32_video.ps1 -InputFile C:\Videos\clip.mp4 `
    -NoAudioNormalization
```

The result is `out\video.hlv`. Uncompressed 16 kHz audio consumes 16,000 bytes
per second in addition to the compressed video. The optional helper copies the
video into the card's `/HLV` directory, verifies it and writes the mandatory
`/HLV/play.txt` selection when the card is mounted as a Windows drive:

```powershell
.\scripts\copy_video_to_sd.ps1 -DestinationRoot E:\
```

For the MJPEG profile, encode the approved 1080p MOV at a 320-pixel width with
its aspect ratio and native frame rate preserved:

```powershell
.\scripts\encode_big_buck_bunny_mjpeg.ps1
.\scripts\copy_video_to_sd.ps1 -DestinationRoot E:\ `
    -InputFile .\out\BigBuckBunny_1080p_mjpeg_q5_native-fps_320x180.avi
```

For BPV1, the corresponding script uses the same approved 1080p source,
preserves native 24 fps and writes BPV1 v6 with four 2-bit block modes,
one-byte motion, unified RAW records and active per-GOP palettes. BPV scripts
use the CUDA encoder by default; pass `-Device Auto` or `-Device Cpu` to an
underlying general-purpose BPV wrapper only when CPU fallback is required:

```powershell
.\scripts\encode_big_buck_bunny_bpv.ps1
.\scripts\copy_video_to_sd.ps1 -DestinationRoot E:\ `
    -InputFile .\out\BPV\BigBuckBunny_1080p_bpv1_v6_four-mode_lambda64_normalized_native-fps_320x180.bpv1
```

The stable wrapper output remains v6. Pass `-PixelMotion` to
`transcode_bpv6.ps1` for experimental v7. Its one-byte vector is measured in
pixels. The decoder keeps one previous display-native RGB565 frame and copies
motion pixels directly, regardless of palette boundaries. The compressed
current frame is consumed through a fixed 4 KiB refill buffer and expands into
two alternating eight-row SPI/DMA buffers. The 1200-byte 320x240 mode map is
retained, but compressed-input capacity is independent of packet size. Once a
buffer has been displayed and its rows are outside the ±7 motion window, it is
copied in-place over the corresponding rows of the previous reference and
reused. The RGB565 reference itself is allocated in independent eight-row
pages so ESP32 never needs a contiguous 153.6 KiB heap block. No current RGB
framebuffer or decoder-side color search is needed. The RGB888 active palette
is converted once per keyframe into a 2 KiB RGB565 lookup table. RAW and block
dictionary reconstruction then reads ready-to-display `uint16_t` values
instead of converting every output pixel. The v7 hot path resolves four local
colors once per block and expands packed selectors a complete four-pixel row
at a time.

The v7 timing record appends compressed-input, block reconstruction and
reference-commit durations plus input call/byte counts to the ordinary `F`
record. `capture-player-metrics.ps1` accepts both the legacy six-value record
and this extended record, writes the additional columns to CSV and prints each
BPV category separately.

On the physical board, two identical 300-frame 320x240 30 fps runs with the
CPU1 input ring averaged 98.0 and 98.3 us in the decoder's input callback,
about 10,916 us complete decode and 19,396 and 19,384 us total work. Neither
run had a frame gap, display skip or work interval beyond the 33,333-us frame
period. The immediately preceding prefetch-free run averaged 6,709 us input,
16,888 us decode and 25,614 us total work.

For an ESP32-safe MPEG Program Stream:

```powershell
.\scripts\encode_mpeg1.ps1 `
    -InputFile .\out\sources\VID_20260522_181611.mp4 `
    -OutputFile .\out\video.mpg
```

This creates the default 240x180 MPEG-1 Video at the native nominal frame rate
with no B pictures, plus normalized MP2 mono at 32 kHz. Add
`-Width 320 -Height 240` for the maximum supported frame size. See
[`MPEG1_PROFILE.md`](MPEG1_PROFILE.md) for the memory limit, validation and
dual-core scheduling details.

### H.263 encoding rule

Encode new H.263 assets only as **baseline H.263 in AVI**, using one of the two
standard picture sizes:

- QCIF: `176x144`;
- CIF: `352x288`.

The encoder always preserves the full source frame rate. It has no half-rate
preset and refuses a source above the supported 30 fps limit rather than
silently dropping frames. `-FitMode Crop` fills the 4:3 frame by cropping equal
margins; `-FitMode Contain` retains the complete picture with black padding.
Both modes fit at the original source resolution and then perform one Lanczos
downscale to the complete QCIF or CIF frame. CIF is intra-only for the bounded
ESP32 decoder memory profile. The saved production default is constant-quality
Q6; pass `-VideoQuality 0` only when an explicit CBR/VBV profile is required.

Create a CIF AVI with PCM S16LE mono audio at 8 kHz:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\out\sources\VID_20260522_181611.mp4 `
    -OutputFile .\out\video_cif.avi `
    -Profile 352x288
```

The AVI reader skips muxer timing chunks, streams video and audio through
separate file cursors, and converts PCM16 to PCM_U8 for the DAC without
retaining the AVI index in RAM.

The decoder still accepts older 3GP/AMR-NB and custom-size H.263+ assets for
backward compatibility. Those combinations are not valid targets for new
encodes. The 3GP reader caches sample-size and chunk-offset tables, so its
memory use also grows with clip length.

The retired custom `320x240` 15 fps profile was exercised for 900 frames on
the physical ESP32. This historical run measured 14.999 fps, 62.20 ms average
and 67.43 ms p95 work per frame against a 66.67 ms budget. It had zero decode
gaps, audio rebuffers, underrun samples, or silence chunks and omitted one
display transfer.

The direct-from-source AVI/PCM variant completed the same 900-frame physical
test at 15.007 fps. Average work was 59.11 ms, p95 was 63.31 ms, and the run
had zero decode gaps, audio rebuffers, underrun samples, or silence chunks,
with one omitted display transfer.

Status text uses an embedded 5x7 font covering every printable ASCII character
from space (`0x20`) through tilde (`0x7e`), including all digits and
punctuation.

The BPV decoder stores two compact 9-byte records per 4x4 block plus bounded
block/pattern dictionaries and a maximum-size packet buffer. At 320x180 the
complete core allocation is about 106 KiB. The conservative packet bound uses
9 bytes per block because `RAW_DIRECT` supports 5–16 colors; adaptive
one-to-four-color RAW records still use only 2/4/7 bytes in real streams. It
renders source rows directly into the two existing display DMA strips, so no
115,200-byte RGB565 frame is allocated. With PCM_U8 audio it uses the DAC
clock; video-only streams use the rational ESP timer clock.

The MJPEG decoder likewise does not retain a complete RGB565 frame.
`esp_new_jpeg` emits one aligned RGB565 block at a time straight into one of
the two display DMA strips. The Player therefore retains only the bounded
compressed JPEG packet; it does not allocate the former separate 10,240-byte
strip or 4,096-byte ROM TJpgDec work area. The superseded ROM decoder path has
been removed.

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

The default strict-C99 firmware is the independent project in
`firmware/esp32_2432s028_hlv_player_idf_c`. The preserved C++ implementation
is available in `firmware/esp32_2432s028_hlv_player_idf_cpp` for comparison.
Both use ESP-IDF APIs directly:
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
and requires no button presses: reset is held for 200 ms and GPIO0 for 100 ms.
It writes the bootloader, partition table and application to internal flash.
The uploader defaults to a conservative 460800 baud. Selected videos and
`play.txt` are not flashed; they remain on the removable card.

With the normal player running, a video and its selection file can instead be
written into the card's `/HLV` directory without removing the card:

```powershell
.\upload-video.ps1 -Port COM8 -File ..\..\out\video.hlv
.\upload-video.ps1 -Port COM8 -File ..\..\out\play.txt
```

The command handshake remains at 460800 baud; the verified data-transfer
default is 2 Mbaud. The player stops video and audio, allocates two temporary
32 KiB receive blocks, shows a progress bar, and verifies per-block and
whole-file CRC32 before replacing the target. Upload protocol v2 uses both
blocks as a sliding window. The PC retains unacknowledged packets for
Go-Back-N retransmission, while an ACK returns a credit after the corresponding
SD write completes. `HLVWAIT` keeps the connection alive during an SD stall;
the PC retries after two seconds and aborts after ten seconds without
cumulative progress. On this CH340C board, CRC-verified 5.19 MB windowed
transfers with two 32 KiB blocks sustained 122.4 KiB/s at 2 Mbaud and
122.3 KiB/s at 3 Mbaud. The 3 Mbaud mode is available for testing, but 2 Mbaud
remains the default because the combined upload pipeline prevents a meaningful
gain. An autonomous 16 MiB SD write benchmark using 32 KiB blocks, including
final flush, sync and close, sustained 1897 KiB/s for zeros and 1905 KiB/s for
prefilled deterministic pseudorandom data. Therefore raw SD-card bandwidth is
not the upload bottleneck. `HLVSDBENCH 1 <zero|random> <size-MiB>` exposes this
diagnostic for 1--64 MiB tests and deletes its temporary file before replying.
A 2.5 Mbaud experiment did not enter normal data reception and timed out. The
Windows CH340 driver rejected both 4 and 5 Mbaud with device error 31 before
the first data block. The 1.5 Mbaud, 921600 and 460800 rates remain available
as fallbacks.

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
