# HLV-1 stream syntax v15

HLV v15 is an additive update to stable v14. The file header, frame packet
header, residual coding, quantization and normative Y7/U6/V6+Q4 reference
reconstruction are unchanged. A v15 decoder must also accept v14; versions
1–13 remain outside the stable compatibility set.

## Version and frame types

The 28-byte `HLV1` sequence header stores version 15. A zero version in the
in-memory API selects the current version; serialized streams always carry an
explicit version.

Frame type 2 is `REPEAT`. It is valid only after a successfully decoded frame
and has zero video bits and zero video payload bytes. Packet audio may still
follow in the ordinary audio tail. The decoder returns the previous
reconstruction directly and does not allocate or copy another reference.

## Macroblock mode codes

As in v14, each raster-order macroblock starts at a byte boundary and its mode
is a four-bit value. Codes 0–8 retain their exact v14 meaning. v15 assigns:

| Code | Name | Syntax after the mode |
| ---: | --- | --- |
| 9 | `SKIP_RUN` | Four-bit `run_minus_2`, representing 2–17 SKIP macroblocks |
| 10 | `SPLIT_JOINT` | Four 8x8 predictor records, then one optional 16x16 residual |
| 11 | `RECT_SPLIT` | Orientation, two rectangular predictor records, then one optional 16x16 residual |

Codes 12–15 are reserved and must be rejected.

`SKIP_RUN` never crosses a macroblock row. Every represented macroblock uses
the same zero-motion prediction and normative reference quantization as v14
`SKIP`. A run that extends beyond the padded row is malformed. A single SKIP
continues to use mode 0 because it is already one byte after alignment.

## Joint split prediction

`SPLIT_JOINT` lists four predictors in top-left, top-right, bottom-left,
bottom-right order. Each begins with one `is_inter` bit. Zero selects the
zero-motion predictor. One is followed by the existing v14 motion-vector code;
when frame-global motion is active, the coded vector is relative to it. The
four predicted 8x8 luma regions and their corresponding 4x4 chroma regions
form one macroblock predictor, to which the ordinary optional 16x16 residual
syntax is applied once.

`RECT_SPLIT` begins with one orientation bit: zero means two 16x8 horizontal
halves and one means two 8x16 vertical halves. Each half then has the same
`is_inter` bit and optional v14 motion-vector code. The two predictions are
combined before one ordinary optional 16x16 residual is decoded.

The encoder compares these modes against its best v14 candidate. It may emit
one only when its macroblock bit count is no greater and its weighted
reconstruction error is no greater. This makes v15's new split selection a
compression optimization rather than a quality trade-off.

## Streaming and validation

All v15 syntax remains sequential. It does not require a complete compressed
packet in memory or random access. The ESP32 decoder therefore keeps its one
fixed 7,680-byte refill buffer, including for packets larger than that buffer.
The following are format errors:

- `REPEAT` before the first decoded reference, or with video payload;
- v15-only frame types or modes in a v14 stream;
- `SKIP_RUN` outside a P-frame or beyond the current row;
- split motion that addresses samples outside the padded reference;
- any non-zero byte-alignment padding or reserved mode.

## Measured acceptance sample

The deterministic 320x240, 30 fps, 60-frame picture-rich regression source
was encoded with the balanced quality-55 profile and one encoder worker. v14
was 1,262,210 bytes; v15 was 1,259,870 bytes, a 2,340-byte (0.185%) reduction.
Average YUV420 PSNR changed from 49.393532 dB to 49.393962 dB. The ESP32
simulator decoded all 60 frames bit-exact across expanded, compact,
257-byte-refill and single-reference segmented paths. Xtensa QEMU decoded its
30-frame v15 benchmark at 2,252,229 cycles/frame average with final hash
`cff1e112a17aba41`.

The same 60-frame file was CRC-verified and played to the loop boundary on the
physical ESP32-2432S028: `count=60`, `gaps=0`, and no display skips. At QVGA
30 fps the current full player is not real-time: decode averaged 61.812 ms and
decode/render work averaged 98.004 ms. The test-only SD file was deleted after
the run, the original 43-byte `play.txt` was never modified, and the ordinary
production build was reflashed with all flash-region hashes verified.
