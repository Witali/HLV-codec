# BPV1 reference codec

BPV1 is the BPAL-derived experimental codec supplied for the multi-codec
laboratory. This package contains the version 2 JavaScript reference encoder
and decoder, automatic 64-palette training, rate-distortion block selection,
strict stream inspection, Y4M command-line adapters, and tests.

The fixed v2 profile uses:

- 4x4 pixel blocks;
- 64 shared palettes with 16 RGB888 colors each;
- four local colors and a 2-bit pixel pattern per block;
- `SKIP`, exact block motion, full-block dictionary, pattern dictionary and
  raw block modes;
- periodic keyframes that reset prediction and dictionaries;
- encoder-side selection by `J = RGB SSE + lambda * estimated payload bits`.

The decoder remains compatible with legacy BPV1 v1 streams containing 16
shared palettes. See [BPV1_FORMAT_ru.md](BPV1_FORMAT_ru.md) for the byte-level
format and [RATE_DISTORTION_ru.md](RATE_DISTORTION_ru.md) for the RD rule.

## Test

BPV1 has no third-party runtime dependency. Node.js 18 or newer is sufficient:

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

## Encode and decode

The command-line adapter consumes the same normalized Y4M video stream used by
the shared benchmark:

```sh
ffmpeg -hide_banner -loglevel error -i input.mov -an \
  -vf "fps=12,scale=320:200:flags=lanczos,format=yuv420p" \
  -f yuv4mpegpipe - \
| node codecs/bpv/tools/bpv1enc.js - output.bpv1 --lambda 64
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

The reference encoder learns one palette bank from all input frames, so it
keeps the normalized RGBA sequence in host memory. The command-line decoder
does not retain decoded frames: it keeps compact 9-byte block records and
renders one frame at a time.

BPV1 currently carries video only. Audio must be measured or transported
separately until a shared multi-codec audiovisual container is defined.

## Reference measurement

The compact reports retained under
[`reference/synthetic-60s/`](reference/synthetic-60s/) describe the supplied
720-frame, 320x200, 12 fps experiment. The selected lambda 64 stream measured
4,220,561 bytes (562.74 kbit/s), 0.733 bit/pixel/frame and 32.95 dB RGB PSNR.
The large generated BPV1 and MP4 files are intentionally not stored in Git.

The provenance and current licensing status of the user-supplied materials are
recorded in [PROVENANCE.md](PROVENANCE.md).
