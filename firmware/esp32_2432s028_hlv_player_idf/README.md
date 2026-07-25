# Multi-codec player for ESP32-2432S028 — pure ESP-IDF

This is a repository-local ESP-IDF 5.5.5 project for the two-USB CYD board. It
does not use Arduino, LovyanGFX or globally installed Espressif tools. The
application supports HLV-1, standard AVI/MJPEG with PCM_U8 audio, Microsoft
MPEG-4 v3 (`DIV3`/`MP43`) AVI with optional PCM_U8 audio, BPV1 v1 through v4
with PCM_U8 audio and active per-GOP palettes, and the constrained MPEG-1
Video/MP2 profile up to 320x240. It also supports baseline H.263 at `176x144`
and intra-only H.263+ at `256x144`, `256x192`, `320x180`, or `320x240`, with
optional 8 kHz mono AMR-NB audio in 3GP or PCM S16LE audio in AVI.

AVI is the preferred H.263 container for this firmware. Its interleaved video
and PCM chunks are streamed without retaining an AVI index in RAM. 3GP remains
supported for AMR-NB and compatibility, but its sample-size and chunk-offset
tables consume memory proportional to the number of samples, reducing the
margin available for long or high-bitrate CIF playback.

The only application components are:

- `main`: ST7789 SPI2 DMA, microSD SPI3 DMA, continuous DAC audio and player;
- `hlv1`: a vendored decoder-only snapshot of the portable HLV codec;
- `bpv1`: the shared portable BPV decoder from `codecs/bpv`;
- `divx3`: the shared portable Microsoft MPEG-4 v3 decoder and AVI reader;
- `pl_mpeg`: the pinned MPEG-PS, MPEG-1 Video and MP2 decoder.
- `h263_3gp`: the shared bounded-table 3GP/AVI demultiplexer, streaming AVI
  PCM reader, and PacketVideo H.263 decoder from `codecs/h263`.
- `amrnb_3gp`: the companion `samr` demultiplexer and PacketVideo AMR-NB
  decoder from `codecs/amrnb`.

`MINIMAL_BUILD` in `CMakeLists.txt` admits only their transitive ESP-IDF
dependencies, excluding Wi-Fi, Bluetooth, networking, NVS and OTA. The
`sdkconfig.defaults` profile also disables coredumps, the task watchdog,
FreeRTOS software timers, trace facilities, long FAT names and the per-file
FatFs cache; it limits FatFs to one volume and VFS to three registrations.
UART0 at 460800 remains enabled for compact per-frame diagnostics. The default
dual-core pipeline pins ordered HLV, BPV, MPEG-1 or H.263 decoding to APP CPU
(CPU1), while the main task on PRO CPU (CPU0) converts the preceding frame to
RGB565 and queues its SPI DMA strips. H.263/3GP sample sizes and chunk offsets
are cached at open time so the hot path reads compressed video sequentially.
Predictive P-frames are never decoded out of order. Dual-core mode cannot add
the 8 KiB RTC Fast RAM to the heap. Slow
exception-emulated byte access to ordinary IRAM stays disabled.
ESP-IDF libraries retain size optimization, while the latency-sensitive
`main`, `hlv1`, `bpv1` and `divx3` components explicitly use `-O3`.

The initial DivX 3 profile is deliberately limited to 256x144 at 12 fps, I/P
pictures, a maximum 96 KiB compressed packet, and no per-macroblock quantizer
changes. DivX 4 and DivX 5 use MPEG-4 Part 2 ASP and are not handled by this
decoder.

## Build and flash

All generated dependencies are placed below this directory in `.tools`:

```powershell
.\setup.ps1
.\build.ps1
.\flash.ps1 -Port COM8
.\monitor.ps1 -Port COM8
```

## Uploading videos to microSD over UART

Prepare the validated DivX 3 profile from the approved 1080p Big Buck Bunny
source. This also writes `out\play.txt`:

```powershell
.\scripts\encode_big_buck_bunny_divx3.ps1
```

The normal player listens for a binary upload command on the same UART0/CH340C
connection used by the monitor. Close the serial monitor, leave the board in
normal application mode, and run:

```powershell
.\upload-video.ps1 -Port COM8 `
    -File ..\..\out\BigBuckBunny_1080p_mjpeg_q5_native-fps_320x180.avi
.\upload-video.ps1 -Port COM8 -File ..\..\out\play.txt
```

BPV1 uses the same upload path:

```powershell
.\upload-video.ps1 -Port COM8 `
    -File ..\..\out\BigBuckBunny_1080p_bpv1_v2_lambda64_native-fps_320x180.bpv1 `
    -Name bunny.bpv1
Set-Content play.txt "bunny.bpv1" -Encoding ascii
.\upload-video.ps1 -Port COM8 -File play.txt
```

The wrapper uses `pyserial` from this project's local ESP-IDF Python
environment; `setup.ps1` installs it under this project, so no global Python
package is required. The control handshake uses 460800 baud and block data uses
2000000 baud by default. The verified fallback values are 1500000, 921600 and
460800 baud.

List the files currently stored in `/sdcard/HLV` without stopping playback:

```powershell
.\list-files.ps1 -Port COM8
.\list-files.ps1 -Port COM8 -Json
```

The client sends `HLVLIST 1` at the 460800-baud control rate. The firmware
returns one `HLVFILE 1 <size> <name>` record per regular file, enclosed by
`HLVLISTBEGIN 1` and `HLVLISTEND 1 <count>`.

The destination defaults to the source filename. `/sdcard/HLV/play.txt`
contains the one video filename that the player opens:

```powershell
.\upload-video.ps1 -Port COM8 -File clip.avi
Set-Content play.txt "clip.avi" -Encoding ascii
.\upload-video.ps1 -Port COM8 -File play.txt
```

Names are ASCII, end in `.hlv`, `.avi`, `.bpv1`, `.mpg`, `.mpeg`, `.3gp` or
`.txt`,
and are at most 48
characters. The player never guesses a fallback file. If `play.txt` is absent
or invalid, it displays `NO SELECTED FILE.` and waits.

The player finishes the current decode operation, stops video and audio, and
closes both SD file cursors before acknowledging an upload. The screen shows a
progress bar during the transfer. Each 60 KiB block has its own CRC32 and is
acknowledged before the PC sends the next block, so hardware flow control is
not required. CRC calculation uses the ESP32 ROM table implementation. The
complete file CRC32 is checked before the previous target is replaced; an
interrupted or corrupt upload leaves the existing video intact. The 60 KiB
buffer exists only during an upload, while the decoder and audio buffers are
released. After each transfer the player reads `/HLV/play.txt` again and opens
its selection.

Protocol version 1 starts with this ASCII line at the console baud:

```text
HLVPUT 1 <name> <size> <crc32-hex> <data-baud>
```

The device replies `HLVREADY 1 61440 <data-baud>`, receives acknowledged
`HLVB` binary blocks, and finishes with
`HLVDONE 1 <size> <crc32> <name>`.

The connected CH340C board completed three CRC-verified transfers at every
supported rate. With the original 4 KiB blocks, 921600, 1500000 and 2000000
baud delivered 70.4, 91.3 and 101.5 KiB/s. Enlarging the block to 16 KiB raised
the 2 Mbaud result to 106.6 KiB/s. The retained 60 KiB block and ROM CRC32
delivered 111.3 KiB/s throughout a continuous 8 MiB transfer. An experimental
2.5 Mbaud transfer timed out, so 2 Mbaud is the maximum verified setting for
this board and driver.

The repository-level wrappers run the same commands:

```powershell
.\setup.ps1
.\scripts\build_esp32.ps1
.\scripts\upload_esp32.ps1 -Port COM8
```

The upload scripts enter download mode automatically through the CH340C
control lines. The project supplies `esptool.cfg` with the sequence for the
EN-to-GND capacitor modification. The profile was verified in four consecutive
ROM connections: hold reset for 200 ms, switch DTR before RTS without an
intentional pause, and hold GPIO0 low for 100 ms. No button presses are
required. See
[`docs/board/CH340C_AUTO_BOOT_MOD.md`](../../docs/board/CH340C_AUTO_BOOT_MOD.md)
for the measured timing limits, current profile, and manual fallback.

## Per-frame timing mode

The current measurement build sets `kLogFrameTimings` in
`main/player_settings.hpp` and restricts ordinary ESP-IDF output to errors.
UART0 emits one CSV record for every decoded frame. A zero `render_us` means
the frame was decoded for prediction but its late display transfer was omitted:

```text
V,width,height,fps_num,fps_den,audio_rate,frame_count
#frame,sd_us,decode_us,render_us,work_us,present_us
F,1,540,18320,26740,45600,108220
```

The `V` record comes from the active HLV, AVI, DivX 3, BPV or MPEG-1 sequence
metadata. The collector uses its rational `fps_num/fps_den` value to calculate
the work budget instead of assuming 15 fps. It also prints the observed
decoded-frame cadence and counts late display transfers omitted by the
real-time mode.

`work_us` is the sum of packet read, decode and render work. `present_us`
measures presentation from entry through A/V-clock waiting and display
submission; packet read and decode occur earlier and can overlap adjacent
frames in dual-core mode. All timestamps are captured before the CSV line is
written, so its own UART transmission is excluded from that frame's values.

With audio enabled, every 30th frame also emits:

```text
A,frame,queued,pending,played,rebuffers,underrun_samples,silence_chunks,loop_events,loop_chunks,mp2_decode_frames,mp2_decode_us,mp2_convert_us
```

The last three cumulative fields separately measure `plm_decode_audio()` and
the subsequent float-to-mono-PCM_U8 conversion. The collector reports both
averages per MP2 frame; non-MPEG formats leave these fields at zero.

The local collector resets the application without entering the ROM
bootloader, rejects frame-sequence gaps, and fails if any rebuffer or missing
audio sample is reported:

```powershell
.\capture-player-metrics.ps1 -Port COM8 -Frames 900 -TimeoutSeconds 120
```

The ST7789 and SD-card SPI clocks are available under the `HLV player`
section of ESP-IDF menuconfig:

```powershell
.\idf.ps1 -IdfArguments @("menuconfig")
```

Clean builds default to an 80 MHz display clock and a 40 MHz SD-card clock.
Reduce either value if the board or card shows transfer errors.

After one or two consecutive SD read failures, the player closes and reopens
the selected video after the normal two-second retry delay. A third failure
before any successfully presented frame additionally unmounts FAT, removes
the SDSPI device, releases SPI3 and recreates the complete storage stack on
the next retry. The board has no software-controlled SD power switch, so this
reinitializes the interface but does not power-cycle the card.

Place the video and `play.txt` in the FAT16/FAT32 card's `/HLV` directory.
Neither file is written to internal flash.

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

For a difficult-GOP test, `hlvpeakdec` ranks every frame with the same
architecture-independent work model as `hlvbenchdec`. The wrapper selects the
GOP containing the highest-ranked frame, prepares a contiguous clip beginning
at its keyframe, and runs that clip through the same Xtensa benchmark:

```powershell
.\qemu-peak-benchmark.ps1 -InputFile ..\..\out\video.hlv
.\qemu-peak-benchmark.ps1 -InputFile input.hlv -StartFrame 4500 -Frames 30
```

Omit `-StartFrame` for automatic selection. An explicit start must name a
keyframe. `-BitReaderBits` and `-Optimization` accept the same values as the
representative benchmark.

On success the guest prints one compact record:

```text
B,frames,avg,p50,p95,max,key_count,key_avg,key_max,p_count,p_avg,p_max,fps_milli,hash,heap,largest
```

The cycle fields come from the guest ESP32 `CCOUNT` register and cover
`decode()` only. The hash covers all reconstructed Y/U/V bytes and is the
bit-exact correctness guard. When the normal firmware profile selects QIO,
Espressif QEMU reports that its virtual flash cannot set the QE bit and
continues in DIO; this is expected and does not invalidate decoder instruction
comparisons.

The first run installs QEMU under this project's `.tools` directory. Generated
clips and QEMU builds are excluded from Git. Guest cycle ratios are useful for
32-bit Xtensa A/B comparisons, but absolute playback speed still requires the
physical board because QEMU is not cycle-accurate and does not model SD or
display DMA timing.

## Resource choices

- Display: ST7789 at configurable 80 MHz. Normal playback uses two reusable
  320x16 RGB565 DMA strips; H.263 reuses one allocation as two 320x8 strips
  and releases the second allocation before creating the decoder.
- Storage: the file named by `/sdcard/HLV/play.txt`, read over SDSPI DMA at
  configurable 40 MHz with a dynamically allocated aligned read-ahead buffer
  (4 KiB for MPEG-1/H.263, 16 KiB for the other formats). HLV uses nine reusable
  7680-byte packet blocks (67.5 KiB); MJPEG uses the maximum indexed
  JPEG chunk size, a 320x16 RGB565 strip and a 4 KiB TJpgDec work area; BPV
  uses one bounded maximum-size packet buffer. The Big Buck Bunny q5 AVI needs
  39,678 bytes for these three MJPEG allocations instead of a full-frame
  design's 144,638 bytes.
- Video: two packed Y6/U5/V5 4:2:0 frames, one signed Q4 local correction per
  8x8 plane block and a macroblock-row work area; 141,120 bytes at 320x180
  instead of 184,320 bytes for two 8-bit frames. The 2,880-byte correction
  tables preserve each block's discarded average to 1/16 sample. Stream v13
  literal blocks are copied directly into this packed storage with zero
  correction. BPV instead retains two 32,400-byte block-record frames plus its
  bounded dictionaries; the complete BPV decoder allocation is about 105 KiB
  at 320x180 and has no full RGB frame.
- DivX 3: two padded 8-bit YUV420 reference frames use 141,008 bytes at
  256x144, plus one compressed packet capped at 96 KiB. The first profile is
  decoded sequentially on CPU0; physical-board frame-time validation is still
  required.
- MPEG-1: two packed Y6/U5/V5 reference frames, one signed Q4 local correction
  per 8x8 plane block and one 8-bit macroblock row (174,480 bytes total at
  320x240). The correction tables add 3,600 bytes and preserve the discarded
  local average to 1/16 sample during prediction and presentation. The player
  also uses bounded 4 KiB read-ahead and elementary buffers plus a separate
  audio-only PL_MPEG instance. Packed planes use separate allocations. Files
  larger than 320x240 or containing B pictures are rejected by the saved
  profile.
- H.263: two separately allocated YUV420 outputs in dual-core mode, allowing
  CPU1 to decode frame N+1 while CPU0 presents frame N. The 320x240 pair uses
  230,400 bytes without requiring either frame to be contiguous. If the
  second custom-profile output cannot be allocated, playback automatically
  uses the one-buffer sequential path. 3GP also retains compact sample-size
  and 64-bit chunk-offset caches to avoid per-frame metadata seeks.
- Scheduling: one 4 KiB CPU1 decoder task, one 3 KiB high-priority CPU0 audio
  reader and two one-entry decode queues for HLV, BPV, MPEG-1 or H.263. MJPEG
  and DivX 3 use the sequential CPU0 path. Only frame descriptors cross cores
  in the pipelined paths, so no frame or packet payload is copied.
- Audio: a static 4 KiB stream buffer feeding a permanent ring of six
  256-sample DAC DMA descriptors directly from the completion ISR. A second
  sequential file cursor skips compressed video and prefetches only PCM packet
  tails, or decodes MP2 with its video stream disabled. The DAC sample count is
  the master video clock. Frame targets are
  calculated from `fps_num/fps_den` in the HLV header. The current real-time
  mode keeps audio continuous and omits the display transfer of a late frame;
  predictive decoding still runs so subsequent P-frames remain valid.
- Flash: one 1.5 MiB factory application partition; no NVS or OTA partition.

`kH263CifPresentationMode` in `main/player_settings.hpp` selects the central
320x240 coded crop or display of the complete 352x288 CIF frame. CIF uses a
12:11 sample aspect ratio, so the complete frame represents a 384x288 (4:3)
picture and fills the 320x240 panel. `kH263ScalingFilter` selects
nearest-neighbour or bilinear filtering. The bilinear CIF path uses
compile-time source-index and Q12 coefficient tables while preparing each
output strip on CPU0, without allocating a scaled framebuffer. H.263
decompression runs concurrently on CPU1. Nearest-neighbour is the default; the
full-panel physical-board test measured 29.3 fps, while bilinear measured
16.1 fps and prioritizes smoothness over frame rate.
The older `kScaleVideoToDisplay` setting continues to control stretching for
the other video codecs.
`kUseCompactY6U5V5` selects the compact decoder and is `true` in the current
test build. Set it to `false` to restore bit-exact 8-bit YUV420 references.
`kUseDualCorePipeline` selects the CPU1-decode/CPU0-render pipeline and is also
`true`; set it to `false` to compare against sequential playback without
changing the HLV file.
`kEnableAudio` enables PCM_U8 playback through DAC GPIO26. The current test
build sets it to `true`; the 4 KiB FreeRTOS stream buffer is statically
allocated. Preroll is calculated from `kAudioPrerollFrames` and the rational
frame rate stored in the file; the current four-frame target covers about
167 ms at 24 fps. Files without audio, an explicitly disabled output, or a
failed audio reader/DAC automatically use the monotonic ESP timer as the video
clock. The periodic log reports queued bytes, controlled rebuffer events,
silence DMA chunks and cyclic-repeat activity.
`kAvSyncMode` selects between `kDropLateVideoFrames`, which keeps audio
continuous and omits late display transfers; `kLoopAudioForLateVideo`, which
presents every frame and repeats the six already allocated DMA descriptors
while video catches up; and `kDropThenLoopAudio`, which omits at most
`kMaxConsecutiveVideoSkips` display transfers before switching to the same DMA
repeat. The hybrid mode is enabled with a two-frame skip limit. Predictive
decoding always continues, repeated samples do not advance the media clock,
and queued source samples resume without being discarded. No mode allocates
additional frame or audio buffers.
