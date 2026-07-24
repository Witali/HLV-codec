# MPEG-1 player profile

The Windows and ESP32 players accept a standard MPEG Program Stream (`.mpg`
or `.mpeg`) containing:

- MPEG-1 Video, YUV420, at most 320x240 visible pixels;
- only I and P pictures (`-bf 0`), with a GOP of 30 by default;
- a standard MPEG frame rate recognized by PL_MPEG;
- one optional MPEG-1 Audio Layer II stream, decoded to mono PCM_U8 at
  playback time.

The ESP32 limit is deliberate. At 320x240, each packed Y6/U5/V5 reference
frame occupies 81,600 bytes. The no-B decoder stores two such frames plus one
7,680-byte 8-bit macroblock-row work area, for 170,880 bytes total. The six
packed planes are separate allocations, so playback does not depend on one
170 KiB contiguous heap block. Both the stdio read-ahead and PL_MPEG
elementary-stream buffer are bounded at 4 KiB. Larger pictures and streams
containing B pictures are outside this profile.

The compact reference format is an ESP32 build option; the Windows player
retains full 8-bit YUV420 references. Y6/U5/V5 rounding can introduce a small
quality loss and predictive drift compared with the desktop decode.

The player uses the MIT-licensed PL_MPEG source pinned in
`third_party/pl_mpeg`. Local changes, their upstream commit and the license are
documented in that directory.

## Encoding

Use the saved profile:

```powershell
.\scripts\encode_mpeg1.ps1 `
    -InputFile .\out\sources\VID_20260522_181611.mp4 `
    -OutputFile .\out\video.mpg
```

For the checked source used during MPEG-1 integration, the preset wrapper
fixes the source path, dimensions and output naming:

```powershell
.\scripts\encode_vid_20260522_181611_mpeg1.ps1
```

Both scripts accept `-MaxFrames` for a short smoke encode. The preset also
exposes video quality, GOP, thread count, MP2 bitrate, output path and report
path without duplicating the encoding pipeline.

Defaults are 240x180, centered 4:3 crop, native nominal frame rate, MPEG-1
quality 3, GOP 30, no B pictures, eight FFmpeg threads, and MP2 mono at 32 kHz
and 64 kbit/s. The script uses the project's primary audio curve: a gentle
-20 dB/1.6:1 compressor followed by measured compressor makeup to a -0.1 dBFS
peak. Video and processed audio are muxed by the same final FFmpeg invocation.

Use `-Width 320 -Height 240` for the maximum device resolution. The smaller
default remains useful when decode throughput matters more than resolution.

After encoding, the script:

1. enumerates all video picture types and rejects any B picture;
2. decodes the complete video and audio streams with FFmpeg;
3. writes an adjacent FFprobe JSON report.

The profile uses 2048-byte MPEG-PS packs so the 4 KiB decoder buffers retain
headroom for packet parsing.

## ESP32 scheduling

The video PL_MPEG instance has audio disabled and the audio reader uses a
second file cursor with video disabled. This prevents either task from
allocating the other stream's decoder.

With `kUseDualCorePipeline=true`, the ordered MPEG-1 decoder runs on CPU1. Its
two packed buffers alternate between the previous reference and the next
decode target. CPU0 simultaneously expands a copied descriptor for the
preceding frame to RGB565 and submits 16-row SPI DMA strips. The payload is
not copied, and the next picture is not started until both the decode and
render stages have completed. The MP2 reader remains a high-priority CPU0
task feeding the existing DAC stream buffer.

The physical ESP32 successfully decoded the 120-frame 320x240/30 fps smoke
file without frame-sequence gaps. The optimized decoder averaged 61.5 ms per
frame and achieved about 15.5 decoded frames/s, so full-QVGA MPEG-1 is
memory-safe but not real-time at 30 fps on this board. Moving the hot decoder
path to IRAM improved decode time by only about 0.5% while consuming 19.6 KiB
of IRAM, so that experiment was not retained.

### Chroma-row reuse

Each YUV420 chroma row belongs to two adjacent luma rows. The compact render
path previously unpacked the same Cb/Cr row and recalculated its RGB
contributions for both luma rows. It now keeps the unpacked row and its three
conversion terms until the chroma-row index changes. At 320x240 this avoids
38,400 packed-sample iterations and 76,800 integer multiplications per frame.
The cache consumes 1,936 bytes of DRAM; 132,276 bytes remain available in the
linked firmware.

Two physical 120-frame runs measured 29.59 and 29.60 ms average render time,
versus 35.70--35.85 ms before the change. Total measured per-frame work fell
from 98.13--99.57 ms to 90.92--90.96 ms, and both runs retained all 120 frame
sequence records. Since video decoding and rendering use different cores, the
end-to-end decoded rate improved more modestly, from 15.08--15.39 to
15.67 frames/s, while CPU0 gained substantially more time for audio and display
service.

### Fixed-point evaluation

The video hot path is already integer: VLC parsing, motion compensation,
residual reconstruction, IDCT and RGB565 conversion do not use floating-point
math. The remaining per-frame `double` operation only updates PL_MPEG's
unused presentation timestamp. Removing it measured 61.70 ms/frame in the
clean hardware run versus 61.61--61.73 ms/frame for the original code, so it
was not retained.

MP2 synthesis is the significant floating-point path. An experimental Q-format
window replaced 36,864 float multiply-accumulates per MP2 frame with scaled
integer operations. Its host output retained 79.7 dB SNR relative to the float
decoder, but the ESP32 hardware result regressed from 19.23 ms to
20.11--20.45 ms per MP2 frame and increased the 120-frame test's underrun from
1,280 to 5,632 samples. ESP32 LX6 has a hardware single-precision FPU, while
the extra scaling and float-to-integer conversions outweighed the integer
windowing gain. The production decoder therefore keeps the float MP2 path.

### Mono MP2 synthesis

PL_MPEG used to copy mono subband samples to a second channel and run the full
IDCT/window synthesis twice. The local decoder now synthesizes a mono frame
once and duplicates only the final PCM samples, while retaining the original
two-channel public output layout. Allocation, scale-factor and unused-subband
work for the second channel is skipped as well.

The old and new decoders produced bit-identical float PCM for both a mono
120-frame device fixture and a generated stereo fixture. On the physical ESP32,
two 120-frame mono runs averaged 17.70 and 17.74 ms per MP2 frame, versus
19.23 ms before the change: a repeatable 7.8% reduction in audio decode CPU
time without changing the samples. Stereo decoding continues through the
unchanged two-channel synthesis path.

### PCM output conversion

The final float-to-PCM_U8 loop formerly called `lroundf()` for every sample.
After clamping, the shifted value is always positive, so adding 0.5 before the
integer conversion has the same rounding result without a library call. The
mono and stereo fixtures had zero differing output bytes. On the ESP32, the
separately measured conversion fell from 451.5 us per MP2 frame to 162.8 and
163.4 us in two runs, a 63.9% reduction. The firmware now reports this
cumulative time as `mp2_convert_us` alongside MP2 decode time.

## Windows validation

The native player needs no external codec at runtime:

```powershell
.\scripts\build_windows_player.ps1
.\build\msvc\hlvplay.exe .\out\video.mpg
.\build\msvc\hlvplay.exe --check .\out\video.mpg
if ($LASTEXITCODE -ne 0) { throw "MPEG-1 validation failed" }
```

MPEG seeking resets the bounded decoder and decodes forward from the start,
discarding frames and MP2 samples before the selected timestamp. This is
slower than HLV/BPV keyframe indexing but keeps video and audio aligned.
