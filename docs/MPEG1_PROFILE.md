# MPEG-1 player profile

The Windows and ESP32 players accept a standard MPEG Program Stream (`.mpg`
or `.mpeg`) containing:

- MPEG-1 Video, YUV420, at most 240x180 visible pixels;
- only I and P pictures (`-bf 0`), with a GOP of 30 by default;
- a standard MPEG frame rate recognized by PL_MPEG;
- one optional MPEG-1 Audio Layer II stream, decoded to mono PCM_U8 at
  playback time.

The ESP32 limit is deliberate. A 240x180 picture occupies a padded 240x192
YCbCr allocation of 69,120 bytes. The no-B decoder stores two such reference
frames (138,240 bytes), instead of the three frames needed for general MPEG-1
with B pictures. Input and elementary-stream buffers are bounded at 8 KiB.
Larger pictures and streams containing B pictures are outside this profile.

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

After encoding, the script:

1. enumerates all video picture types and rejects any B picture;
2. decodes the complete video and audio streams with FFmpeg;
3. writes an adjacent FFprobe JSON report.

The profile uses 2048-byte MPEG-PS packs so the 8 KiB decoder buffers retain
headroom for packet parsing.

## ESP32 scheduling

The video PL_MPEG instance has audio disabled and the audio reader uses a
second file cursor with video disabled. This prevents either task from
allocating the other stream's decoder.

With `kUseDualCorePipeline=true`, the ordered MPEG-1 decoder runs on CPU1. Its
two buffers alternate between the previous reference and the next decode
target. CPU0 simultaneously converts a copied descriptor for the preceding
frame to RGB565 and submits 16-row SPI DMA strips. The payload is not copied,
and the next picture is not started until both the decode and render stages
have completed. The MP2 reader remains a high-priority CPU0 task feeding the
existing DAC stream buffer.

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
