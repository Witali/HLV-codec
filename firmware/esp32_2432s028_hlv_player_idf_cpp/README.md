# Multi-codec player for ESP32-2432S028 — pure ESP-IDF

This directory preserves the C++ implementation for comparison. The default
strict-C99 implementation is in `../esp32_2432s028_hlv_player_idf_c`.

This is a repository-local ESP-IDF 5.5.5 project for the two-USB CYD board. It
does not use Arduino, LovyanGFX or globally installed Espressif tools. The
application supports HLV-1, standard AVI/MJPEG with mono WAV IMA ADPCM or
legacy PCM_U8 audio, Microsoft MPEG-4 v3 (`DIV3`/`MP43`) AVI with the same
optional audio profiles, BPV1 v1 through v6 with PCM_U8 or project IMA ADPCM
audio, active per-GOP palettes and unified RAW blocks, and the
standard MPEG-1 Video/MP2 I/P/B profile up to 320x240. It also
supports baseline H.263 at `176x144` QCIF or intra-only baseline `352x288`
CIF, with
optional 8 kHz mono AMR-NB audio in 3GP or WAV IMA ADPCM audio in AVI.
MPEG-4 Part 2 Simple Profile is supported at `320x240` in M4S2 AVI with I/P
pictures and the same optional AVI IMA audio.

The audio output is configured anew from each opened file's sample rate. PDM
playback accepts 8–48 kHz, including 22.05, 44.1 and 48 kHz; playlist items do not
need to share one rate. Diagnostics report the actual rate and the resulting
time capacity of the fixed byte queue. Before media playback, the signed-PCM
PDM bias rises linearly from 0 V to the half-supply midpoint over 100 ms. The
inverse ramp is drained through the DMA ring before I2S is disabled, and GPIO26
is then held low, avoiding a step at either end of a clip.

New project H.263 assets use only baseline H.263 in AVI at standard QCIF
`176x144` or CIF `352x288`, always at the full source frame rate. Legacy
3GP/AMR-NB remains a decoder-only compatibility path for those same standard
geometries; all H.263+ custom-size modes are rejected. AVI's
interleaved video and audio chunks are streamed without retaining an index in
RAM; the legacy 3GP reader's sample-size and chunk-offset tables consume memory
proportional to the number of samples.

The only application components are:

- `main`: ST7789 SPI2 DMA, microSD SPI3 DMA, I2S PDM audio and player;
- `hlv1`: a vendored decoder-only snapshot of the portable HLV codec;
- `bpv1`: the shared portable BPV decoder from `codecs/bpv`;
- `avi_demux`: the one shared C99 RIFF/AVI parser and packet router used by
  MJPEG, DivX 3, H.263 and MPEG-4 SP;
- `divx3`: the portable Microsoft MPEG-4 v3 video decoder;
- `pl_mpeg`: the pinned MPEG-PS, MPEG-1 Video and MP2 decoder.
- `h263_3gp`: the bounded-table 3GP demultiplexer, AVI stream adapter, and
  PacketVideo H.263/MPEG-4 SP decoder from `codecs/h263`.
- `amrnb_3gp`: the companion `samr` demultiplexer and PacketVideo AMR-NB
  decoder from `codecs/amrnb`.

`MINIMAL_BUILD` in `CMakeLists.txt` admits only their transitive ESP-IDF
dependencies, excluding Wi-Fi, Bluetooth, networking, NVS and OTA. The
`sdkconfig.defaults` profile also disables coredumps, the task watchdog,
FreeRTOS software timers, trace facilities, long FAT names and the per-file
FatFs cache; it limits FatFs to one volume and VFS to three registrations.
UART0 at 460800 baud, 8 data bits, no parity and 2 stop bits remains enabled
for compact per-frame diagnostics. The default
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
| HLV v14/v15 video | One reusable 7,680-byte refill buffer used by `hlv1_decoder_decode_file()` | Compliant; packets may exceed the buffer |
| H.263/MPEG-4 SP video | One reusable 4 KiB PacketVideo callback/refill buffer | Compliant; AVI/3GP samples may exceed the buffer. Only MPEG-4 VOL configuration is retained contiguously, capped at 256 bytes |
| MPEG-1 video and MP2 audio | PL_MPEG file and elementary ring buffers, initially 4 KiB | Streaming, but PL_MPEG can reallocate an elementary ring to fit a large PES packet; keep the encoded profile PES-bounded and remove this growth when changing the core |
| Player PCM_U8/PCM_S16LE audio | One 4 KiB FreeRTOS stream buffer filled in at most 512-byte reads | Compliant |
| AVI WAV IMA ADPCM audio | Standard blocks up to 2 KiB are decoded directly into the PCM16 stream through one 128-byte compressed refill buffer | Compliant; the production 1024-byte block exceeds both the refill and one SD sector |
| Legacy AMR-NB audio | One complete compressed sample, strictly limited to 32 bytes | Documented atomic-frame exception; streaming would not reduce meaningful memory |
| BPV v1-v6 video | One complete bounded frame packet, including palette/modes/payload/audio | Technical debt: add a core file/refill API that retains only palette and mode metadata and streams the sequential payload; preserve or deliberately replace the current CPU1 packet prefetch |
| BPV v7 video | Fixed 8 KiB FreeRTOS stream buffer filled by CPU1 in 4 KiB SD reads, followed by the decoder's reusable 4 KiB refill buffer | Compliant; both capacities are independent of the maximum encoded frame |
| DivX 3 video | One reusable 4 KiB decoder refill buffer over a bounded AVI packet span | Compliant; packets may exceed the refill buffer, the player caps their span at 96 KiB, and no packet payload is copied between tasks |
| MJPEG video | One reusable configurable refill buffer, 8,192 bytes by default | Compliant; the project replaces only `esp_new_jpeg` entropy input, so packets may exceed the buffer while the existing IDCT, RGB565 and block-output paths remain active |

AVI container traversal itself is sequential and retains no chunk index.
Legacy 3GP retains compact sample-size and chunk-offset metadata, but H.263
sample payloads are still decoded through the 4 KiB refill buffer.

## Build and flash

The C++ reference shares the pinned ESP-IDF installation in the sibling C
firmware project's `.tools` directory. Its wrappers still build, flash and
monitor this C++ project:

```powershell
.\setup.ps1
.\build.ps1
.\flash.ps1 -Port COM8
.\monitor.ps1 -Port COM8
```

The shared `TONE_TEST` CMake mode also builds the SD-free 1 kHz GPIO26 DAC
sine-ramp diagnostic. Use the primary C firmware's `tone-test.ps1` wrapper as
the supported build-and-flash entry point for this hardware test.

The shared `PDM_TONE_TEST` mode replaces the internal DAC with I2S0 PCM-to-PDM
data on GPIO26 while leaving the external clock output unpinned. Use the primary
C firmware's `pdm-tone-test.ps1` wrapper as the supported entry point.

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

The wrapper uses `pyserial` from the shared project-local ESP-IDF Python
environment; `setup.ps1` prepares it through the primary C firmware project,
so no global Python package is required. The application control channel and
block data both use 460800 baud with 8N2 framing by default, so normal commands
do not transition the UART speed. The verified `HLVBAUD` command remains available
for an explicit temporary change when diagnostics require another rate.

Every exchange has an explicit lifecycle. The PC sends
`HLVSESSION 1 <command>`, waits for `HLVSESSIONREADY 1 <command>`, and only then
sends the command payload. ESP32 stops playback and diagnostics before READY.
After the exchange, the PC sends `HLVMONITOR 1 ON` and waits for
`HLVMONITORREADY 1 ON`; only then does the ESP32 restore diagnostics and reopen
the selected video. `resume-monitoring.ps1` sends this final command manually
after an interrupted client:

```powershell
.\resume-monitoring.ps1 -Port COM8
```

List the files currently stored in `/sdcard/HLV`:

```powershell
.\list-files.ps1 -Port COM8
.\list-files.ps1 -Port COM8 -Json
```

The client sends `HLVLIST 2` after the LIST session is ready. Each `HLVL`
binary record contains sequence, file size, UTF-8 name length, name and CRC32.
The client ACKs every record; a zero-length final record carries the count.

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

Read a complete file, or only a byte range, without removing the SD card:

```powershell
.\read-file.ps1 -Port COM8 -Name play.txt -Output .\play.txt
.\read-file.ps1 -Port COM8 -Name video.avi -Offset 1024 -Length 256 `
    -Output .\video-header.bin
```

Change the UART control rate explicitly when needed:

```powershell
.\set-baud.ps1 -Port COM8 -ToBaud 921600
.\set-baud.ps1 -Port COM8 -FromBaud 921600 -ToBaud 460800
```

`HLVBAUD 1 <baud>` uses a verified handshake at both the old and new rates,
then remembers the new control rate until another `HLVBAUD` command or a
reset. The read client uses this command before and after a transfer whenever
`-DataBaud` differs from 460800. `uart_read.py` and `uart_upload.py` share the
same handshake timings and retry count from `uart_baud.py`; control records at
the selected high rate are repeated, while binary blocks retain CRC and
sequence validation.

The client sends `HLVREAD 2 <name> <offset> <length> <data-baud>`. The firmware
validates the regular filename and range, then streams the data in reusable
64-byte payload blocks. Each binary block starts with `HLVX`, a
little-endian sequence number, payload length, and payload CRC32. The final
`HLVREADDONE 2 <size> <crc32> <name>` also verifies the entire returned range.
The client acknowledges each valid block and requests retransmission after a
CRC error. The default data rate is the same 460800 baud used by the rest of
the application UART; `-DataBaud` can explicitly select and verify another
supported rate.
When `-Length` is omitted, the client requests everything from `-Offset`
through EOF. Existing local files are preserved unless `-Force` is supplied.

Rewrite one byte range while preserving the rest of an existing SD file:

```powershell
.\patch-file.ps1 -Port COM8 -Name video.avi -Offset 32764928 `
    -File .\correct-range.bin
```

`HLVPATCH 1` transfers 4 KiB CRC-protected packets to a temporary file. The
firmware backs up the exact target range, applies the patch, flushes it, and
rereads the range from SD before reporting success. A failed verification
restores the backup.

To detect and repair only corrupt regions of a large file, compare 64 KiB
blocks on both endpoints and patch adjacent mismatches:

```powershell
.\sync-file.ps1 -Port COM8 -File .\video.avi -DryRun
.\sync-file.ps1 -Port COM8 -File .\video.avi
```

`HLVBLOCKCRC 1` calculates every block CRC and the whole-file CRC directly on
the ESP32. The client acknowledges each protected result, patches only ranges
whose CRC differs, then repeats the complete block and whole-file scan before
declaring success.

To remove one explicitly named regular file, start a `DELETE` session and send
`HLVDELETE 1 <name>`; the firmware rejects paths and hidden names and returns
`HLVDELETE 1 <name>` only after success. Send `HLVMONITOR 1 ON` to reopen
`play.txt` afterwards.

The autonomous SD write benchmark uses:

```text
HLVSDBENCH 1 <zero|random> <size-MiB>
```

Start an `SDBENCH` session before sending this command. The size is limited to
1--64 MiB. The player stops playback, pre-fills one
32 KiB block with zeros or deterministic pseudorandom bytes, writes it
directly to a temporary file, and includes `fflush`, `fsync` and `fclose` in
the elapsed time. The temporary file is deleted before the result is returned.
On the installed card, 16 MiB tests delivered 1897 KiB/s for zeros and
1905 KiB/s for pseudorandom data. Both temporary files were confirmed absent
through the directory listing command.

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

Names are ASCII, end in `.hlv`, `.avi`, `.bpv1`, `.mpg`, `.mpeg` or `.txt`,
and are at most 111
characters. The player never guesses a fallback file. If `play.txt` is absent
or invalid, it displays `NO SELECTED FILE.` and waits.

The physical BOOT button can select a video without a PC. A short press during
normal playback stops the current file and opens the `/sdcard/HLV` browser.
The screen shows five left-aligned filenames in case-insensitive lexicographic
order. The selected row stays in the middle whenever two filenames exist on
either side, and moves toward an edge only near the start or end. Each
subsequent short press advances by one file and scrolls the five-row window,
wrapping after the last file. Holding
BOOT for at least 800 ms writes the selected filename to `play.txt` and starts
it. The browser lists regular `.hlv`, `.bpv1`, `.avi`, `.mpg`, `.mpeg`, `.3gp`
and `.3gpp` files without retaining the complete directory in memory.
Holding BOOT while resetting still requests the ESP32 ROM download mode.

The player finishes the current decode operation, stops video and audio, and
closes both SD file cursors before acknowledging an upload. During the transfer
the screen shows a large completion percentage above the progress bar and the
transferred/total size beside it using three significant digits, with the
destination filename below the bar.
Each 4096-byte block has its own CRC32. Upload protocol v2 advertises a two-block
sliding window, so the PC can send both receive buffers without waiting for an
individual ACK. An ACK is cumulative and returns buffer credit only after the
CPU1 SD writer completes that block. A NAK causes Go-Back-N retransmission from
the rejected sequence. During an SD stall the ESP32 sends `HLVWAIT` every
250 ms; the client retries after a two-second ACK timeout and aborts after ten
seconds without cumulative progress. Hardware flow control is therefore not
required. CRC calculation uses the ESP32 ROM table implementation. The
complete file CRC32 is accumulated while receiving the upload. After `fsync`,
the temporary file is reopened and reread from SD to verify that CRC before
the previous target is replaced; an interrupted or corrupt upload leaves the
existing video intact. Two reusable 4096-byte buffers exist only during an upload,
while the decoder and audio buffers are released. CPU0 receives and validates
the next UART block while a CPU1 writer task stores the preceding block on SD.
An ACK means that a block passed its RAM CRC and completed its SD write;
`HLVDONE` is emitted only after both buffers are written, `fsync` completes and
the SD readback full-file CRC matches. The video remains stopped until the explicit
`HLVMONITOR 1 ON` command.

The upload client first uses `HLVBAUD` to put both endpoints at the selected
data rate. Upload protocol version 2 then starts with this ASCII line:

```text
HLVPUT 2 <name> <size> <crc32-hex> <data-baud>
```

The device replies `HLVREADY 2 4096 <data-baud> 2`, receives windowed `HLVB`
binary blocks, reports `HLVACK`, `HLVNAK` or `HLVWAIT`, and finishes with
`HLVDONE 2 <size> <crc32> <name>`. The client explicitly restores 460800 baud
with `HLVBAUD`, then enables monitoring after successful completion.

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
Those measurements describe earlier large-block experiments. The current
application and tools default to 460800-baud 8N2 framing and
64-byte packets; 3000000 baud remains an explicit experimental mode. The
Windows CH340 driver rejected attempts to
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
decoded-frame cadence and counts the optional late BPV display transfer
omitted immediately before a known I-frame.

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
the subsequent float-to-mono-PCM_S16 conversion. The collector reports both
averages per MP2 frame; non-MPEG formats leave these fields at zero.

### Compact YUV-to-RGB565 fast path

The native, aligned Y6/U5/V5 renderer in
`codecs/common/src/y6u5v5_rgb565.c` processes 16x2 luma tiles and writes RGB565
pairs directly into the LCD DMA strip. MPEG-1, DivX 3 and MPEG-4 Simple Profile
all adapt their compact planes to this one C implementation. Baseline H.263
currently produces ordinary YUV420 and keeps its reference row renderer. The
fused path avoids complete unpacked Y/U/V rows, reuses chroma for both YUV420
rows, and preserves the reference Q4 correction pattern bit for bit. Scaled or
unaligned pictures automatically use the reference row renderer.

`COMPACT_YUV_RGB565_FAST_PATH`, `COMPACT_YUV_RGB565_CLAMP_TABLES`,
`COMPACT_YUV_RGB565_HOT_IRAM` and `COMPACT_YUV_Q4_LUT` default to `ON`. The Q4
table is shared by Y, U and V and remains 368 bytes; values outside the
encoder-produced `-8..14` range retain the exact calculated fallback.

The local collector resets the application without entering the ROM
bootloader, rejects frame-sequence gaps, and fails if any rebuffer or missing
audio sample is reported:

```powershell
.\capture-player-metrics.ps1 -Port COM8 -Frames 900 -TimeoutSeconds 120
```

Seek the selected video to an absolute position in milliseconds with the
standalone UART command:

```text
HLVSEEK 1 60000
```

or with the local wrapper:

```powershell
.\seek-video.ps1 -Port COM8 -Milliseconds 60000
```

The firmware reopens the selected video and restores predictive decoder state
without displaying or pacing the intermediate frames. Independent MJPEG
packets are skipped without decoding. Compressed audio is consumed to the same
timeline before normal A/V-synchronised playback resumes. The board reports
`HLVSEEKBEGIN 1 requested_ms target_frame` followed by
`HLVSEEKDONE 1 requested_ms actual_ms frame`; a request beyond a known
duration is clamped to the last frame. For a timed measurement window, the
collector can issue the same command immediately after reset:

```powershell
.\capture-player-metrics.ps1 -Port COM8 -SeekMilliseconds 60000 `
    -Frames 1800 -TimeoutSeconds 600
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

The C++ wrappers use the same sibling `C:\Work\QEMU-ESP32` runtime as the
primary C firmware. `setup-qemu.ps1` delegates to that project's setup and
`idf.ps1` places its `bin` directory ahead of any legacy `.tools` QEMU. Set
`HLV_QEMU_ESP32_ROOT` when the QEMU checkout is elsewhere.

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

The MPEG-1 benchmark similarly embeds 60 frames of the validated 320x180
Program Stream by default and runs the Player's compact `pl_mpeg` component.
A custom input may use any valid constrained-profile size up to 320x240, so
the slow maximum-resolution production streams can be used for A/B tests:

```powershell
.\qemu-mpeg1-benchmark.ps1
.\qemu-mpeg1-benchmark.ps1 -InputFile input.mpg -Frames 60
```

It prints an `M` record containing the decode-only cycle distribution,
reconstructed-frame hash, free heap and largest free block. The preparation
step copies the original MPEG-1 video packets without re-encoding and rejects
the clip unless its dimensions and frame count match the requested profile.

The two heap fields describe this isolated decoder benchmark, not the normal
player's RAM headroom. This build links only the embedded MPEG bytes, the
compact-YUV helper and `pl_mpeg`; it does not link or initialize the SD/FAT,
display/DMA, I2S/audio-queue, UART-service, worker-task or other-codec state of
`hlv_player.cpp`. Compare normal-player memory with the normal ELF linker map
and capability-heap snapshots from the physical board. Also, QEMU's ESP32
`-m 4M` option creates an emulated PSRAM device; this project has
`CONFIG_SPIRAM` disabled, so that device is not added to the ESP-IDF heap and
does not explain the benchmark's larger free-heap value.

`PLM_MPEG_DECODE_PROFILE` is a build-time CMake option and defaults to `OFF`.
When enabled, all timing reads and counters are compiled into the decoder and
an `MDP` record is printed every 60 decoded frames. Its cumulative fields are
frames, CPU MHz, total, coefficient/VLC, reconstruction/IDCT, motion and
compact-storage cycles, followed by block, DC-only block, general-IDCT block
and motion-macroblock counts. Do not compare instrumented timing directly
with a release build.

`PLM_MPEG_IRAM_COMPACT_MC` defaults to `ON` and places only the two O3
compact motion-compensation kernels in IRAM. On the physical 240 MHz ESP32 it
reduced average decode time by 7.77% on Danila 320x240 q41 and by 7.81% on the
independent MPEG-1 Regression clip. It costs 10,352 bytes of IRAM; the normal
player link retains 12,704 bytes before the SRAM1 boundary. Disable the option
for an IRAM-constrained diagnostic build.

`PLM_MPEG_DCT_SECOND_LEVEL` defaults to `ON`. It adds 280 bytes of generated
VLC data for only the five DCT coefficient prefixes unresolved by the existing
six-bit table. Four short prefixes use 7- or 8-bit lookup; the long `000000`
branch resolves its 10- and 12-bit codes directly and retains the complete
tree fallback for 13- to 16-bit codes. Physical decode improved by 2.42% on
Danila q41 and 2.59% on Regression. The full application grew by 352 bytes,
with no IRAM or heap-start change.

The H.263 benchmark embeds 60 frames of the validated intra-only CIF AVI,
including its IMA ADPCM audio stream, and uses one output frame because no
display pipeline overlaps the decode:

```powershell
.\qemu-h263-benchmark.ps1
.\qemu-h263-benchmark.ps1 -InputFile input.avi -Frames 60
```

It prints an `H` record containing the decode-only cycle distribution,
reconstructed YUV420 hash, decoder allocation, free heap and largest free
block. The preparation step copies video and audio without re-encoding and
rejects any clip that is not H.263 at 352x288 with the requested frame count
or does not preserve the source audio profile.

The MPEG-4 SP benchmark embeds 60 frames of a validated `320x240` M4S2 AVI,
uses two compact Y6/U5/V5 I/P pictures plus one 16-row reconstruction
workspace, and requires a packet larger than the decoder's 4 KiB refill
buffer:

```powershell
.\qemu-mpeg4-benchmark.ps1
.\qemu-mpeg4-benchmark.ps1 -InputFile input.avi -Frames 60
```

It prints an `M` record and `MPEG4_BENCH_DONE,0`; its reconstructed YUV420
hash can be compared directly with the primary C firmware.

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

The production default is `MJPEG_STREAMING_INPUT=ON` with an 8,192-byte
buffer. `MJPEG_INPUT_BUFFER_BYTES` can be set from 1,024 through 65,536 bytes;
QEMU measurements with 16,384 bytes improved decode time by only about 0.1%,
so the larger allocation is not the default. Compare a smaller refill buffer
with the legacy contiguous control:

```powershell
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -StreamingInput ON -InputBufferBytes 4096
.\qemu-mjpeg-benchmark.ps1 -Frames 60 -StreamingInput OFF
```

Use an input whose maximum JPEG packet is greater than the configured buffer;
the complete RGB565 hash must equal the contiguous control.

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

The first run installs QEMU under the primary C firmware project's shared
`.tools` directory. Generated clips and QEMU builds are excluded from Git.
Guest cycle ratios are useful for
32-bit Xtensa A/B comparisons, but absolute playback speed still requires the
physical board because QEMU is not cycle-accurate and does not model SD or
display DMA timing.

For full board-path tests, the C++ reference firmware shares the patched
SDSPI/ST7789/I2S Windows QEMU maintained by the primary C firmware project.
Its modeled-device inventory and known accuracy limits are recorded in
[`docs/QEMU_ESP32_PLAYER_PERIPHERAL_AUDIT.md`](../../docs/QEMU_ESP32_PLAYER_PERIPHERAL_AUDIT.md).

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
  each packet through one reusable 7,680-byte refill buffer; MJPEG uses one
  configurable 8,192-byte `esp_new_jpeg` refill buffer and writes RGB565
  blocks directly into the two display DMA strips, without a separate
  320x16 strip or the 4 KiB ROM TJpgDec work area. The buffer is allocated
  once when playback begins, reused for every frame without resizing or
  per-frame allocation, and freed when playback ends. BPV uses one bounded
  maximum-size packet buffer.
- HLV video: packed Y7/U6/V6 4:2:0 references, one signed Q4 local correction
  per 8x8 plane block and a macroblock-row work area. Profiles up to 320x192
  keep two pointer-swapped references; that uses 164,160 bytes at 320x180
  instead of 184,320 bytes for two 8-bit frames. Larger profiles use one
  complete previous frame plus 32 rolling current luma rows. At 320x240 this
  single-reference strategy uses 118,520 bytes, 84,760 fewer than two compact
  references, while CPU0 waits only for source rows needed by rendering.
  Correction tables preserve each block's discarded average to 1/16 sample.
  Stable HLV v14/v15 makes this compact reconstruction normative, so packed and
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
  also uses a bounded 4 KiB video buffer and a fixed 1 KiB audio elementary
  refill plus a separate audio-only PL_MPEG instance. MP2 PES payloads larger
  than the refill are consumed incrementally without growing the buffer. The
  large MP2 synthesis state is allocated before that refill. At 320x240 MPEG
  reuses the permanent 320x16 display allocation as two 320x8 DMA strips
  instead of retaining a second 10 KiB strip. Smaller profiles retain the
  faster pair of 16-row strips. Packed planes use separate allocations. B
  pictures are non-reference frames and are rendered synchronously from the
  macroblock-row workspace, avoiding a third full frame. Files larger than
  320x240 are rejected by the saved profile.
- H.263: two separately allocated YUV420 outputs in dual-core mode, allowing
  CPU1 to decode frame N+1 while CPU0 presents frame N. The 320x240 pair uses
  230,400 bytes without requiring either frame to be contiguous. If the
  second custom-profile output cannot be allocated, playback automatically
  uses the one-buffer sequential path. 3GP also retains compact sample-size
  and 64-bit chunk-offset caches to avoid per-frame metadata seeks.
- MPEG-4 Simple Profile: two independently allocated Y6/U5/V5 frames with
  signed Q4 block-average corrections plus one 16-luma-row byte-planar
  reconstruction workspace. A completed macroblock row is packed before the
  workspace is reused, so no full byte-planar MPEG-4 frame is allocated.
  At 320x240 the QEMU decoder reports 193,880 bytes including PacketVideo
  tables, the 4 KiB refill buffer and container state.
- Scheduling: one 4 KiB CPU1 decoder task, one high-priority CPU1 audio reader
  using one reusable static 6 KiB backing stack. MPEG MP2 activates 2.5 KiB of
  it, HLV PCM activates the measured 4 KiB, and the other container and
  compressed-audio paths activate all 6 KiB. The static task remains
  synchronizable until the controller deletes it, avoiding deferred dynamic
  stack reclamation and heap fragmentation across repeated playback. Two
  one-entry decode queues for HLV, BPV,
  MPEG-1, H.263 or DivX 3. MJPEG uses the sequential CPU0 path. Only frame
  descriptors cross cores in the pipelined paths, so no frame or packet
  payload is copied.
- Audio: a static 4 KiB stream buffer feeding a permanent ring of six
  256-sample signed-PCM I2S PDM DMA descriptors directly from the completion
  ISR. A second
  sequential file cursor skips compressed video and prefetches only PCM packet
  tails, or decodes MP2 with its video stream disabled. The PDM DMA sample count is
  the master video clock. Frame targets are
  calculated from `fps_num/fps_den` in the active container. The current
  real-time mode keeps audio continuous and still displays a late frame once
  it has been decoded. After sustained lag, HLV, packet-based BPV, DivX 3,
  MPEG-1, H.263 and MPEG-4 SP discard future compressed predictive pictures
  before reconstruction until the next independently decodable picture.
  When BPV already exposes that the next packet is an I-frame, a late pending
  predecessor may omit its display transfer so the I-frame gets priority.
  MJPEG pictures are independent, so any picture that is already at least one
  frame late is discarded before JPEG reconstruction; no already-decoded
  MJPEG picture is withheld from the display. The BPV v7 direct stream retains
  ordered decoding.
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
`kEnableAudio` enables PCM_U8, PCM_S16LE, directly synthesized signed PCM16 MP2,
and decoded IMA ADPCM playback through I2S0 PCM-to-PDM on GPIO26 data with the
external clock output unpinned. The current test
build sets it to `true`; the 4 KiB FreeRTOS stream buffer is statically
allocated. Playback starts, and resumes after an underrun, only after that
queue is filled completely. This holds 128 ms at 32 kHz, 256 ms at 16 kHz or
512 ms at 8 kHz for one-byte PCM; signed PCM16 MP2 holds 64 ms at 32 kHz and
decoded PCM16 IMA holds about 43 ms at 48 kHz. Files without audio, an
explicitly disabled output, or a
failed audio reader/PDM output automatically use the monotonic ESP timer as the video
clock. The periodic log reports queued bytes, controlled rebuffer events and
silence DMA chunks; the two legacy DMA-repeat fields remain zero for collector
compatibility. Audio is always the uninterrupted master clock and an already
consumed DMA block is never replayed. An already decoded late frame is still
sent to the display. After three consecutive late intervals, future compressed
predictive pictures are discarded without reconstruction until the next
keyframe; that keyframe is decoded and shown even if video is still behind.
For pipelined BPV, whose next keyframe flag is known in advance, the one late
decoded frame immediately before that keyframe may omit its display transfer.
MJPEG pictures are independent, so a picture that is at least one frame behind
the audio or timer clock is skipped before JPEG reconstruction and display DMA.
Each compressed skip advances the presentation timeline and is counted
separately in the playback summary.
