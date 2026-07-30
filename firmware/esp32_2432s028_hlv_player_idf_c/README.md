# Multi-codec player for ESP32-2432S028 — pure ESP-IDF

This is a repository-local ESP-IDF 5.5.5 project for the two-USB CYD board. It
does not use Arduino, LovyanGFX or globally installed Espressif tools. The
application supports HLV-1, standard AVI/MJPEG with PCM_U8 audio, Microsoft
MPEG-4 v3 (`DIV3`/`MP43`) AVI with optional PCM_U8 audio, BPV1 v1 through v6
with PCM_U8 audio, active per-GOP palettes and unified RAW blocks, and the
constrained MPEG-1 Video/MP2 profile up to 320x240. It also
supports baseline H.263 at `176x144` QCIF or intra-only baseline `352x288`
CIF, with
optional 8 kHz mono AMR-NB audio in 3GP or PCM S16LE audio in AVI.

The strict C99 migration plan, preserved C++ baseline and physical all-codec
A/B acceptance matrix are documented in
[`../../../docs/ESP32_C99_MIGRATION.md`](../../../docs/ESP32_C99_MIGRATION.md).

New project H.263 assets use only baseline H.263 in AVI at standard QCIF
`176x144` or CIF `352x288`, always at the full source frame rate. Legacy
3GP/AMR-NB remains a decoder-only compatibility path for those same standard
geometries; all H.263+ custom-size modes are rejected. AVI's
interleaved video and PCM chunks are streamed without retaining an index in
RAM; the legacy 3GP reader's sample-size and chunk-offset tables consume memory
proportional to the number of samples.

For AVI, the player reads the first video stream's `strh.fccHandler` FourCC
from `LIST hdrl` once. It selects H.263, DivX 3 or MJPEG from that header,
rejects unsupported or missing handlers, and keeps the selected decoder for
the rest of the file. It does not probe several decoders against media
packets.

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

The DivX 3 profile is limited to 320x240 at 12 fps, I/P pictures, a maximum
96 KiB compressed packet, and no per-macroblock quantizer changes. Its two
references use packed Y6/U5/V5 samples with signed Q4 block-average
corrections. DivX 4 and DivX 5 use MPEG-4 Part 2 ASP and are not handled by
this decoder.

## Compressed input buffering

Compressed input should use a fixed-size refill, ring, or stream buffer whose
capacity does not depend on the largest encoded packet. A decoder may retain a
complete packet only when its API requires contiguous or random-access input;
such an exception must have a strict size limit and must not copy the payload
between tasks. A streaming-path test must include a valid packet larger than
the refill buffer and compare the decoded-frame checksum with contiguous
decoding.

The current decoder audit is:

| Path | Compressed input | Status |
| --- | --- | --- |
| HLV v14 video | One reusable 7,680-byte refill buffer used by `hlv1_decoder_decode_file()` | Compliant; packets may exceed the buffer |
| H.263 video | One reusable 4 KiB PacketVideo callback/refill buffer | Compliant; AVI/3GP samples may exceed the buffer |
| MPEG-1 video and MP2 audio | PL_MPEG file and elementary ring buffers, initially 4 KiB | Streaming, but PL_MPEG can reallocate an elementary ring to fit a large PES packet; keep the encoded profile PES-bounded and remove this growth when changing the core |
| Player PCM_U8/PCM_S16LE audio | One 4 KiB FreeRTOS stream buffer filled in at most 512-byte reads | Compliant |
| Legacy AMR-NB audio | One complete compressed sample, strictly limited to 32 bytes | Documented atomic-frame exception; streaming would not reduce meaningful memory |
| BPV v1-v6 video | One complete bounded frame packet, including palette/modes/payload/audio | Technical debt: add a core file/refill API that retains only palette and mode metadata and streams the sequential payload; preserve or deliberately replace the current CPU1 packet prefetch |
| BPV v7 video | Fixed 16 KiB FreeRTOS stream buffer filled by CPU1 in 4 KiB SD reads, followed by the decoder's reusable 4 KiB refill buffer | Compliant; both capacities are independent of the maximum encoded frame |
| DivX 3 video | One reusable 4 KiB decoder refill buffer over a bounded AVI packet span | Compliant; packets may exceed the refill buffer, the player caps their span at 96 KiB, and no packet payload is copied between tasks |
| MJPEG video | One complete indexed JPEG chunk | Documented library exception: `esp_new_jpeg` accepts one contiguous `inbuf` and has no refill callback; the AVI reader enforces the indexed maximum and the decoder writes output in strips |

AVI container traversal itself is sequential and retains no chunk index.
Legacy 3GP retains compact sample-size and chunk-offset metadata, but H.263
sample payloads are still decoded through the 4 KiB refill buffer.

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
    -File ..\..\out\BPV\BigBuckBunny_1080p_bpv1_v6_four-mode_lambda64_normalized_native-fps_320x180.bpv1 `
    -Name bunny.bpv1
Set-Content play.txt "bunny.bpv1" -Encoding ascii
.\upload-video.ps1 -Port COM8 -File play.txt
```

The wrapper uses `pyserial` from this project's local ESP-IDF Python
environment; `setup.ps1` installs it under this project, so no global Python
package is required. The control handshake uses 460800 baud and block data uses
2000000 baud by default. The optional 3000000-baud mode is CRC-verified but
does not improve end-to-end throughput on this board. The verified fallback
values are 1500000, 921600 and 460800 baud.

List the files currently stored in `/sdcard/HLV` without stopping playback:

```powershell
.\list-files.ps1 -Port COM8
.\list-files.ps1 -Port COM8 -Json
```

The client sends `HLVLIST 1` at the 460800-baud control rate. The firmware
returns one `HLVFILE 1 <size> <name>` record per regular file, enclosed by
`HLVLISTBEGIN 1` and `HLVLISTEND 1 <count>`.

Calculate the size and CRC32 of every video directly from the SD card:

```powershell
.\checksum-files.ps1 -Port COM8
.\checksum-files.ps1 -Port COM8 -Json
```

The underlying `HLVCRC 1 <name>` request stops playback and returns
`HLVCRC 1 <size> <crc32> <name>`. Results are appended to
`/sdcard/HLV/crc32.txt` and reused when both the filename and size match, so a
large file is read only once. A successful UART upload also records its
verified CRC immediately. If files are changed outside the player while
keeping the same name and size, remove `crc32.txt` to invalidate the index.
To remove one explicitly named regular file, send `HLVDELETE 1 <name>`; the firmware
rejects paths and hidden names, closes playback before removal, returns
`HLVDELETE 1 <name>` only after success, and then reopens `play.txt`.

The autonomous SD write benchmark uses:

```text
HLVSDBENCH 1 <zero|random> <size-MiB>
```

The size is limited to 1--64 MiB. The player stops playback, pre-fills one
32 KiB block with zeros or deterministic pseudorandom bytes, writes it
directly to a temporary file, and includes `fflush`, `fsync` and `fclose` in
the elapsed time. The temporary file is deleted before the result is returned.
On the installed card, 16 MiB tests delivered 1897 KiB/s for zeros and
1905 KiB/s for pseudorandom data. Both temporary files were confirmed absent
through `HLVLIST 1`.

The destination defaults to the source filename. `/sdcard/HLV/play.txt`
contains the one video filename that the player opens:

```powershell
.\upload-video.ps1 -Port COM8 -File clip.avi
Set-Content play.txt "clip.avi" -Encoding ascii
.\upload-video.ps1 -Port COM8 -File play.txt
```

For repeatable tests, select an uploaded video without leaving a local
`play.txt` file:

```powershell
.\select-video.ps1 -Port COM8 `
    -Name "Danila_320x240_30fps_HLVv14_38dB.hlv"
```

The helper uploads a temporary ASCII selection through the same atomic,
full-file CRC-verified protocol as a normal video.

Names are ASCII, end in `.hlv`, `.avi`, `.bpv1`, `.mpg`, `.mpeg`, `.3gp` or
`.txt`,
and are at most 48
characters. The player never guesses a fallback file. If `play.txt` is absent
or invalid, it displays `NO SELECTED FILE.` and waits.

The physical BOOT button can select a video without a PC. A short press during
normal playback stops the current file and opens the `/sdcard/HLV` browser.
Each subsequent short press advances to the next supported video filename in
case-insensitive lexicographic order and wraps after the last file. Holding
BOOT for at least 800 ms writes the displayed filename to `play.txt` and
starts it. The browser lists regular `.hlv`, `.bpv1`, `.avi`, `.mpg`,
`.mpeg`, `.3gp` and `.3gpp` files; it rescans the directory on every press
instead of retaining the complete list in RAM. Holding BOOT while resetting
the board still requests the ESP32 ROM download mode.

The player finishes the current decode operation, stops video and audio, and
closes both SD file cursors before acknowledging an upload. During the transfer
the screen shows a large completion percentage above the progress bar and the
transferred/total size beside it using three significant digits, with the
destination filename below the bar.
Each 32 KiB block has its own CRC32. Upload protocol v2 advertises a two-block
sliding window, so the PC can send both receive buffers without waiting for an
individual ACK. An ACK is cumulative and returns buffer credit only after the
CPU1 SD writer completes that block. A NAK causes Go-Back-N retransmission from
the rejected sequence. During an SD stall the ESP32 sends `HLVWAIT` every
250 ms; the client retries after a two-second ACK timeout and aborts after ten
seconds without cumulative progress. Hardware flow control is therefore not
required. CRC calculation uses the ESP32 ROM table implementation. The
complete file CRC32 is checked before the previous target is replaced; an
interrupted or corrupt upload leaves the existing video intact. The 60 KiB
buffer was replaced by two 32 KiB buffers that exist only during an upload,
while the decoder and audio buffers are released. CPU0 receives and validates
the next UART block while a CPU1 writer task stores the preceding block on SD.
An ACK means that a block passed its RAM CRC and completed its SD write;
`HLVDONE` is emitted only after both buffers are written, `fsync` completes and
the full-file CRC matches. After each transfer the player reads
`/HLV/play.txt` again and opens its selection.

Upload protocol version 2 starts with this ASCII line at the console baud:

```text
HLVPUT 2 <name> <size> <crc32-hex> <data-baud>
```

The device replies `HLVREADY 2 32768 <data-baud> 2`, receives windowed `HLVB`
binary blocks, reports `HLVACK`, `HLVNAK` or `HLVWAIT`, and finishes with
`HLVDONE 2 <size> <crc32> <name>`.

The connected CH340C board completed three CRC-verified transfers at every
supported rate. With the original 4 KiB blocks, 921600, 1500000 and 2000000
baud delivered 70.4, 91.3 and 101.5 KiB/s. Enlarging the block to 16 KiB raised
the 2 Mbaud result to 106.6 KiB/s. The retained 60 KiB block and ROM CRC32
delivered 111.3 KiB/s throughout a continuous 8 MiB transfer. Double-buffered
16 KiB UART receive and SD writes delivered 44.5, 79.6, 108.5 and 121.0 KiB/s
at 460800, 921600, 1500000 and 2000000 baud in CRC-verified 5.19 MB BPV v7
transfers. The same full transfer passed at 3000000 baud but delivered only
121.3 KiB/s, showing that the upload pipeline, rather than the UART line, is
the current limit. Protocol v2 sliding-window transfers of the same file with
two 32 KiB blocks delivered 122.4 KiB/s at 2000000 baud and 122.3 KiB/s at
3000000 baud, with matching full-file CRC32. The larger window removes the
mandatory per-block wait but raises throughput only marginally. The autonomous
SD benchmark reaches about 1.85 MiB/s, so raw card bandwidth is not the
bottleneck; the remaining limit is in the combined UART/SD pipeline. An
experimental 2500000-baud transfer never entered normal data reception and
timed out.
Therefore 2000000 baud remains the default; 3000000 baud is retained only as
an optional verified mode. The Windows CH340 driver rejected attempts to
configure both 4000000 and 5000000 baud with device error 31, before either
transfer could send its first data block.

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

## SPI SD card in Xtensa QEMU

The repository patch for Espressif QEMU connects an `if=sd` drive to the same
controller and chip-select configuration as the physical ESP32-2432S028:

- `SPI3_HOST`;
- SCK GPIO18, MOSI GPIO23 and MISO GPIO19;
- active-low CS on GPIO5;
- `SPI_DMA_CH_AUTO`;
- the Player's configured 40 MHz SD clock limit.

The stock Espressif QEMU connects `-drive if=sd` to the ESP32 SDMMC model.
Run the following once to clone
`esp-develop-9.2.2-20260417` at commit
`40edccac415693c5130f91c01d84176ae6008566`, apply the SDSPI patch and build
the Xtensa emulator under the repository-root `local_tools/qemu-sdspi`
directory:

```powershell
.\setup-qemu-sdspi.ps1 -InstallWslDependencies
```

Later setup runs do not need `-InstallWslDependencies`. The complete smoke test
builds a small C99 firmware through the normal ESP-IDF SDSPI/FAT path, creates
a 64 MiB FAT32 image, reads its 512-byte FAT sectors through SPI DMA, mounts
it and verifies the file contents:

```powershell
.\qemu-sdspi-test.ps1
```

To boot another merged 4 MiB ESP32 flash image with an existing FAT card
image, omit `-SdImage` to use the tracked five-minute Big Buck Bunny
H.263/AVI demo:

```powershell
.\run-qemu-sdspi.ps1 `
    -FlashImage .\build\qemu_flash_4mb.bin
```

The repository includes the minimal patched emulator as a native, static
Windows executable managed by Git LFS. Run it instead of the WSL build with:

```powershell
.\run-qemu-demo-windows.ps1
```

This dedicated demo launcher builds and merges the C99 production firmware
on its first run, then starts the visible ST7789 window with the tracked demo
SD image. Later runs reuse `build-qemu-demo\qemu_demo_flash_4mb.bin`; pass
`-Rebuild` to refresh it or `-Headless` to run without an SDL window. The
native launcher sends the emulated GPIO26 DAC to Windows DirectSound. Set its
independent QEMU gain with `-Volume 0..100`; the default is 70:

```powershell
.\run-qemu-demo-windows.ps1 -Volume 50
```

The default FAT32 image is
`qemu\hlv-big-buck-bunny-5min-h263-avi.img`. It contains `HLV\bunny.avi`
and a matching `HLV\play.txt`: the first 7200 frames (five minutes) of Big
Buck Bunny as baseline intra-only CIF H.263 at 24 FPS, Q6, with PCM S16LE
mono 8 kHz audio. Recreate it from the approved source with
`.\prepare-qemu-demo-sd.ps1`. The image is tracked with Git LFS. Use
`-SdImage <sd.img>` to run a different card image.

To rebuild or refresh the saved runtime, run
`.\setup-qemu-sdspi-windows.ps1`. It downloads the pinned MSYS2 `20260611`
SFX into repository-local `local_tools/`, verifies its SHA-256 and builds only
`xtensa-softmmu`. The tracked runtime contains the static
`qemu-system-xtensa.exe`, upstream license texts and the two ESP32 ROM blobs.
MSYS2, cloned QEMU sources, build outputs, UART logs and generated card images
outside the tracked demo remain ignored local artifacts.

The patched machine options include `sdspi`, `st7789`, `audiodev`,
`dac-rate` and `dac-volume`. QEMU models SPI3 transfers, DMA descriptor
chains, the interrupt matrix, GPIO output and GPIO5 CS. Its I2S0 model consumes
the same linked DMA descriptors used by ESP-IDF's continuous DAC driver,
extracts the high byte of each 16-bit slot and routes the unsigned 8-bit mono
stream to the selected QEMU audio backend. The internal analog-I2C model also
reports APLL calibration completion so the physical 8 kHz configuration does
not stall. It does not model the electrical ESP32 IO-matrix routing,
physical-card timing or analog GPIO26 circuitry, so it validates the driver
and data paths but is not a cycle-accurate electrical simulation.

The board model also supplies the physical SD MISO pull-up value (`0xff`)
while GPIO5 deasserts CS. Without that state, ESP-IDF's unmodified SDSPI
driver waits for a busy-poll timeout before every command. The complete
peripheral inventory, protocol checks and measured multi-block throughput are
recorded in
[`docs/QEMU_ESP32_PLAYER_PERIPHERAL_AUDIT.md`](../../docs/QEMU_ESP32_PLAYER_PERIPHERAL_AUDIT.md).

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

The DivX 3 benchmark embeds the first 60 frames of the validated QVGA AVI,
creates the same compact Y6/U5/V5 decoder as the player and hashes its
correction-adjusted visible samples:

```powershell
.\qemu-divx3-benchmark.ps1
.\qemu-divx3-benchmark.ps1 -InputFile input.avi -Frames 60
```

It prints a `D` record with the same cycle percentiles, separate I/P timing,
the reconstructed-frame hash, decoder allocation, free heap and largest free
block. The wrapper verifies that the generated embedded clip remains
`msmpeg4v3`/`DIV3`, 320x240 and contains the requested number of frames.

The MPEG-1 benchmark similarly embeds 60 frames of the validated 240x180
Program Stream and runs the Player's compact `pl_mpeg` component:

```powershell
.\qemu-mpeg1-benchmark.ps1
.\qemu-mpeg1-benchmark.ps1 -InputFile input.mpg -Frames 60
```

It prints an `M` record containing the decode-only cycle distribution,
reconstructed-frame hash, free heap and largest free block. The preparation
step copies the original MPEG-1 video packets without re-encoding and rejects
the clip unless its dimensions and frame count match the requested profile.

The H.263 benchmark embeds 60 frames of the validated intra-only CIF 3GP
and uses one output frame because no display pipeline overlaps the decode:

```powershell
.\qemu-h263-benchmark.ps1
.\qemu-h263-benchmark.ps1 -InputFile input.3gp -Frames 60
```

It prints an `H` record containing the decode-only cycle distribution,
reconstructed YUV420 hash, decoder allocation, free heap and largest free
block. The preparation step copies video samples without re-encoding and
rejects any clip that is not H.263 at 352x288 with the requested frame count.

The BPV renderer benchmark generates deterministic 320x240 block records and
measures complete 16-row-strip RGB565 conversion without display DMA:

```powershell
.\qemu-bpv-render-benchmark.ps1
```

It prints a `P` record with the render-only cycle distribution, RGB565 output
hash, free heap and largest free block. The same synthetic frame is rendered
repeatedly so the benchmark isolates renderer changes from decode and SD I/O.
The Player caches the active 64x16 BPV palette as RGB565 and rebuilds the
2 KiB table only for a keyframe, avoiding per-block RGB888 conversion.

The MJPEG benchmark copies a bounded number of original compressed frames
without re-encoding. By default it measures the Player's `esp_new_jpeg`
RGB565 block decoder and direct output:

```powershell
.\qemu-mjpeg-benchmark.ps1
```

The production build selectively places the active Huffman, YUV420 process and
RGB565 packing kernels from the prebuilt `esp_new_jpeg` archive in IRAM. It
also links bit-exact Xtensa DC-only and one-/two-column reduced-row shortcuts
around the fixed 8x8 IDCT. Compare the DC wrapper against the unchanged
library kernel while retaining the same hot-section placement:

```powershell
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -OptimizedIdct OFF
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -OptimizedIdct ON
```

Compare the retained reduced-row extension independently from the DC-only
wrapper:

```powershell
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -ReducedIdct OFF
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -ReducedIdct ON
```

Compare the retained aligned zero-128 coefficient clear against libc:

```powershell
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -FastClear OFF
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -FastClear ON
```

To run the historical all-Flash control variant (which also disables the
IRAM-only IDCT wrapper unless explicitly overridden):

```powershell
.\qemu-mjpeg-benchmark.ps1 -HotIram OFF
```

The `J` record includes per-frame cycles, the complete submitted RGB565 hash
and appended decoder-only, header, geometry, JPEG-process and correctness
callback averages. The callback average measures RGB565 hashing and is
subtracted from `decoder_avg`; phase counters are compiled only into this
benchmark. `MJPEG_OPTIMIZED_IDCT=ON` and
`MJPEG_IDCT_REDUCED_ROWS=ON` are the production defaults. The aligned
coefficient clear is enabled with `MJPEG_FAST_COEFFICIENT_CLEAR=ON`. The old
ROM TJpgDec implementation and its benchmark modes were removed after the
accelerated backend passed the A/B checks. Display SPI/DMA time is measured
only on the physical board.

The first decoder-benchmark run installs the stock QEMU under this project's
`.tools` directory. Generated clips and QEMU builds are excluded from Git.
Guest cycle ratios are useful for 32-bit Xtensa A/B comparisons, but absolute
playback speed still requires the physical board because QEMU is not
cycle-accurate. The custom SDSPI machine above models the storage data path,
but not physical SD or display DMA timing.

## Resource choices

- Display: ST7789 at configurable 80 MHz. Normal playback, including H.263,
  uses two reusable 320x16 RGB565 DMA strips. DivX 3 and BPV v7 reuse one
  allocation as two 320x8 strips and release the second allocation before
  creating the decoder.
- Storage: the file named by `/sdcard/HLV/play.txt`, read over SDSPI DMA at
  configurable 40 MHz with a dynamically allocated aligned read-ahead buffer
  (4 KiB for MPEG-1/DivX 3/H.263, 16 KiB for the other formats). DivX 3 adds
  one reusable 4 KiB decoder refill buffer rather than retaining an AVI
  packet. HLV streams
  each packet through one reusable 7,680-byte refill buffer; MJPEG uses the maximum indexed
  JPEG chunk size and writes `esp_new_jpeg` RGB565 blocks directly into the
  two display DMA strips, without a separate 320x16 strip or the 4 KiB ROM
  TJpgDec work area. BPV uses one bounded maximum-size packet buffer.
- HLV video: packed Y7/U6/V6 4:2:0 references, one signed Q4 local correction
  per 8x8 plane block and a macroblock-row work area. Profiles up to 320x192
  keep two pointer-swapped references; that uses 164,160 bytes at 320x180
  instead of 184,320 bytes for two 8-bit frames. Larger profiles use one
  complete previous frame plus 32 rolling current luma rows. At 320x240 this
  single-reference strategy uses 118,520 bytes, 84,760 fewer than two compact
  references, while CPU0 waits only for source rows needed by rendering.
  Correction tables preserve each block's discarded average to 1/16 sample.
  Stable HLV v14 makes this compact reconstruction normative, so packed and
  expanded decoders predict from identical samples. Literal blocks carry four
  separate Y corrections plus one U and one V correction. BPV instead retains two
  32,400-byte block-record frames plus its
  bounded dictionaries; the complete BPV decoder allocation is about 106 KiB
  at 320x180 with the conservative 9-byte-per-block `RAW_DIRECT` packet bound
  and has no full RGB frame.
- DivX 3: two packed Y6/U5/V5 reference frames, Q4 correction maps and rolling
  DC/AC/MV predictor rows use 174,000 bytes at 320x240, versus 237,600 bytes
  for the exact 8-bit decoder after the same predictor-row optimization.
  Each reference is allocated independently, limiting the largest frame
  request to 83,400 bytes. The player also uses one 10 KiB display allocation,
  4 KiB stdio read-ahead and one reusable 4 KiB decoder refill buffer. AVI
  packet spans are capped at 96 KiB without retaining their payload. It is
  decoded on CPU1 while CPU0 renders the preceding ping-pong output. Physical
  tests completed without sequence gaps or audio underruns. Optimized q4
  playback reaches 12.005 observed fps with no display skips at 320x180.
  A complete 360-frame 320x240 run reaches 11.998 fps with no skips; its
  decode p95 is 67.35 ms and its maximum is 80.30 ms.
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
  reader and two one-entry decode queues for HLV, BPV, MPEG-1, H.263 or DivX
  3. MJPEG uses the sequential CPU0 path. Only frame descriptors cross cores
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

H.263 CIF scaling has been removed. The player starts the visible area at
the macroblock-aligned coordinate `(16,16)` of a 352x288 frame, converts that
320x240 area into the DMA strips on CPU0, and copies it to the panel
pixel-for-pixel. This leaves 16 coded columns on both sides, 16 rows above,
and 32 rows below. H.263 decompression runs concurrently on CPU1. There are no
CIF scaling tables or scaled framebuffer. The central part is deliberately
shown instead of scaling the whole CIF picture, avoiding extra per-frame CPU
work and memory traffic. A 300-frame physical-board run of
the 30 fps CIF AVI measured 29.993 fps with zero display skips or audio errors.
The encoder scripts prepare the active area by cropping or padding the original
large-resolution source to 4:3 and applying one anti-aliased Lanczos downscale
to 320x240. They pad that image at `(16,16)` without a SAR or DAR override.
`kScaleVideoToDisplay` continues to control stretching for the
other video codecs; CIF deliberately ignores it.
`kUseCompactHlvReference` selects the compact decoder and is `true` in the current
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
