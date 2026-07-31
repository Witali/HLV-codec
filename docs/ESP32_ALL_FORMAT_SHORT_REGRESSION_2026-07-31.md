# ESP32 short all-format regression — 2026-07-31

## Corpus

`scripts/test_all_video_formats.ps1` generated a deterministic 60-frame,
320x240, 30 fps source from six photographic scenes. Slow pan/zoom, crossfades,
moving high-contrast shapes, a progress strip, unique frame labels, and a
48 kHz sine source exercise motion, residual, edge, scene-change, and audio
paths. Production wrappers created:

- HLV v14, BPV v6, MJPEG, MPEG-1, and MPEG-4 Simple Profile at 320x240;
- baseline H.263 at standard CIF 352x288 and the full 30 fps source rate;
- DivX 3 at its approved half-rate 15 fps, producing 30 frames.

The host test required complete decoding, exact expected frame counts, and
non-empty normalized audio from the project decoders. The HLV check also
required compact Y7/U6/V6, expanded, 257-byte refill, and single-reference
reconstruction to remain bit-exact.

## Physical ESP32 result

Production C99 firmware commit `950e910` was flashed to the ESP32-2432S028 on
COM8. All three flash segments reported `Hash of data verified`, followed by a
hard reset. Each short clip was uploaded under a unique `Test_...` name,
selected through `play.txt`, and captured through its final frame.

| Format | Decoded | Gaps | Observed fps | Audio result |
| --- | ---: | ---: | ---: | --- |
| HLV v14 | 60/60 | 0 | 19.268 | telemetry clean, no underrun |
| BPV v6 | 60/60 | 0 | 30.194 | telemetry clean, no underrun |
| MJPEG | 60/60 | 0 | 29.738 | telemetry clean, no underrun |
| DivX 3 | 30/30 | 0 | 15.096 | PCM decoded by host; clip ended before the first periodic audio telemetry record |
| MPEG-1 | 60/60 | 0 | 17.728 | audio decoded by host; clip ended before the first periodic audio telemetry record |
| H.263 CIF | 60/60 | 0 | 30.210 | telemetry clean, no underrun |
| MPEG-4 SP | 60/60 | 0 | 16.002 | telemetry clean, no underrun |

The HLV compact decoder and renderer were additionally checked in both C and
C++ QEMU builds for 60/60 frames with identical reconstruction hash
`975db0152c92dc41`. The generated MJPEG clip also completed 60/60 frames in
both QEMU variants with identical hash `411c5da6533f5a40`.

## SD cleanup

After the run, `play.txt` was restored to
`Danila_320x240_30fps_MPEG4SP_35dB.avi`. Exactly the seven test-only files were
deleted with `HLVDELETE 1 <name>`. A fresh `HLVLIST 1` confirmed that none
remained and that all pre-existing assets, `play.txt`, and `crc32.txt` were
preserved.
