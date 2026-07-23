# Encoder SIMD benchmark

The host encoder keeps its scalar reference operations and selects SSE2 only
after a runtime CPU check. `hlvenc --simd off` forces the old scalar pipeline;
`--simd auto` is the default.

The accepted SIMD paths are exact integer replacements for:

- luma SAD in integer- and half-pixel motion search;
- squared-error accumulation used by macroblock and frame RDO.

The benchmark uses 360 consecutive Big Buck Bunny frames beginning at 120
seconds, converted from the project-approved 1080p MOV to 320x180 YUV420 at
24 fps. Each result is the median of three MSVC `/O2` runs using the balanced
v13, quality 45, GOP 30 profile.

| Pipeline | Threads | Median time | Throughput | Change |
|---|---:|---:|---:|---:|
| Scalar baseline | 1 | 21.123 s | 17.04 fps | baseline |
| SSE2 SAD + RDO SSE | 1 | 17.724 s | 20.31 fps | +19.2% |
| Scalar baseline | 4 | 6.180 s | 58.25 fps | baseline |
| SSE2 SAD + RDO SSE | 4 | 5.193 s | 69.32 fps | +19.0% |

Scalar and SIMD outputs have the same SHA-256:
`4D36A41966D3C165EE4B4D0DE58575CCCB9F68A2B9BA8682FD45F2C88150E05D`.
The threaded regression also compares reconstructed Y4M and audio-bearing HLV
output against `--simd off`.

An SSE2 fractional motion-prediction writer was evaluated and rejected. It
preserved the output hash but regressed the medians from 17.724 to 18.042
seconds with one thread and from 5.193 to 5.360 seconds with four threads.
The original scalar fractional predictor therefore remains in use.
