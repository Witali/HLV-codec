# BPV1 codec

BPV1 is the BPAL-derived experimental codec supplied for the multi-codec
laboratory. This package contains a bounded-memory, multi-threaded C11 encoder,
a portable streaming C decoder, the version 2/3/4 JavaScript reference
implementation, automatic active 64-palette training, rate-distortion block
selection, strict stream inspection, Y4M command-line adapters, and tests.

The common video profile uses:

- 4x4 pixel blocks;
- 64 shared palettes with 16 RGB888 colors each;
- four local colors and a 2-bit pixel pattern per block;
- `SKIP`, exact block motion, full-block dictionary, pattern dictionary and
  raw block modes;
- periodic keyframes that reset prediction and dictionaries;
- encoder-side selection by `J = RGB SSE + lambda * estimated payload bits`.

BPV1 v4 trains and transmits an active 64x16 palette bank in every keyframe.
Each GOP can therefore replace colors that are no longer useful for the
current scene. It retains v3 interleaved unsigned 8-bit mono PCM. The decoder
remains compatible with fixed-palette v3, video-only v2 and legacy v1 streams
containing 16 shared palettes. See
[BPV1_FORMAT_ru.md](BPV1_FORMAT_ru.md) for the byte-level format and
[RATE_DISTORTION_ru.md](RATE_DISTORTION_ru.md) for the RD rule.

## Test

The JavaScript reference suite has no third-party runtime dependency. Node.js
18 or newer is sufficient:

```sh
make -C codecs/bpv test
```

or:

```sh
npm --prefix codecs/bpv test
```

The suite covers v2 round-trip, legacy v1 decoding, automatic palette
training, RD selection, command-line Y4M round-trip and truncated-stream
rejection.

Build the native encoder and run its C-to-JavaScript compatibility and
one/four-thread determinism test on Windows:

```powershell
.\scripts\build_bpv_msvc.ps1
```

On a POSIX C11 host:

```sh
make -C codecs/bpv test-c
```

The native test covers all five block modes and row rendering. Passing an
existing file performs a complete sequential C decode:

```powershell
.\build\msvc\test_bpv1_decoder.exe .\out\video.bpv1
```

## Native C encoder

The production encoder uses two bounded-memory passes over a seekable
8-bit YUV 4:2:0 Y4M file. The first pass counts frames. During the second pass,
each independent GOP trains its own 64x16 RGB palette bank from a reservoir
sample, encodes with that bank, and is written in presentation order. GOP
training and encoding run in parallel. Eight worker threads, a 48-frame GOP,
active palettes and lambda 64 are the defaults.

```sh
ffmpeg -hide_banner -loglevel error -i input.mov -an \
  -vf "scale=320:-2:flags=lanczos,setsar=1,format=yuv420p" \
  -fps_mode passthrough -f yuv4mpegpipe input.y4m
./codecs/bpv/bpv1enc input.y4m output.bpv1 \
  --threads 8 --gop 48 --lambda 64 \
  --active-palettes \
  --audio-u8 input-mono-16000.u8 --audio-rate 16000 \
  --report output.json
```

The saved Big Buck Bunny script always reads the project-approved 1080p MOV,
preserves its native frame rate and 16:9 aspect ratio, and produces 320x180:

```powershell
.\scripts\encode_big_buck_bunny_bpv.ps1
```

Decode to Y4M:

```sh
node codecs/bpv/tools/bpv1dec.js output.bpv1 - \
| ffmpeg -hide_banner -loglevel error -f yuv4mpegpipe -i - decoded.mp4
```

Inspect and fully validate a stream without allocating a framebuffer:

```sh
node codecs/bpv/tools/bpv1info.js output.bpv1
```

Analyze file-level 128/256/512-palette banks without modifying the stream:

```sh
node codecs/bpv/tools/bpv1superpalette.js output.bpv1 \
  --bank-sizes 128,256,512 --output superpalette-report.json
```

The native encoder's `--active-palette-file FILE` option is an experimental
test hook. `FILE` contains one consecutive 3,072-byte RGB888 bank for each
GOP. The encoder uses those banks while still writing ordinary BPV1 v4, which
allows controlled A/B tests without adding a production format version. The
completed experiment and rejection measurements are recorded in
[`../../docs/BPV1_SUPERPALETTE_TODO.md`](../../docs/BPV1_SUPERPALETTE_TODO.md).

The older `tools/bpv1enc.js` remains as the compact reference/fallback
pipeline. It learns one palette bank from all input frames and therefore keeps
the normalized RGBA sequence in host memory. The command-line decoder does
not retain decoded frames: it keeps compact 9-byte block records and renders
one frame at a time.

By default the encoder writes v4 with or without audio. `--fixed-palettes` retains the
old v2/v3 output mode and one bank for the whole file. Short PCM inputs are
padded with unsigned silence (`128`), and trailing samples beyond the video
duration are ignored.

## Players

The same `src/bpv1_decode.c` implementation is linked into both players:

```powershell
.\build\msvc\hlvplay.exe .\out\video.bpv1
```

On ESP32, put the `.bpv1` file and a `play.txt` containing its base filename
in `/HLV` on the microSD card. The decoder retains two compact 9-byte block
record frames and renders RGB565 rows directly into the display's existing DMA
strips. At 320x180 its decoder state, maximum packet buffer and dictionaries
consume about 109 KiB and no complete RGB framebuffer. BPV v3/v4 uses the same
PCM_U8 DAC/audio-clock pipeline as HLV; files without audio remain timer-clocked.

## Reference measurement

The compact reports retained under
[`reference/synthetic-60s/`](reference/synthetic-60s/) describe the supplied
720-frame, 320x200, 12 fps experiment. The selected lambda 64 stream measured
4,220,561 bytes (562.74 kbit/s), 0.733 bit/pixel/frame and 32.95 dB RGB PSNR.
The large generated BPV1 and MP4 files are intentionally not stored in Git.

The provenance and current licensing status of the user-supplied materials are
recorded in [PROVENANCE.md](PROVENANCE.md).
