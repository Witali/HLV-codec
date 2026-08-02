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

## Physical decoder retest — 2026-08-02

The current C99 firmware was rebuilt in `build-decoder-matrix` with the tracked
460800-baud, `-O3` configuration and flashed to the same ESP32-D0WD-V3 revision
3.1. Esptool verified the bootloader, partition table and application hashes
before the final hard reset. Every row below is a real-board run through the
production SD, decoder, audio and LCD paths. All clips reached the requested
last frame with zero telemetry frame gaps.

| Decoder/profile | Frames | Observed/source fps | Read ms | Decode ms | Render ms | Work ms | Display skips | Audio/result |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| HLV v14, 320x240 | 60/60 | 19.168/30 | 0 | 52.553 | 30.710 | 83.263 | 10 | Clean PCM; complete but below real time. |
| BPV v6, 320x240 | 60/60 | 29.964/30 | 7.004 | 5.568 | 16.773 | 29.345 | 0 | Clean PCM; real time. |
| DivX 3, 320x240 | 30/30 | 14.972/15 | 0.982 | 54.663 | 36.019 | 91.663 | 0 | Target rate; the short clip ended before periodic audio telemetry. |
| MJPEG streaming, 320x240 | 60/60 | 29.500/30 | 9.855 | 23.420 | 0.796 | 34.071 | 29 | Clean PCM; every packet decoded, but 29 LCD submissions were skipped. |
| MPEG-1/MP2, 320x240 | 60/60 | 17.317/30 | 0 | 57.876 | 32.555 | 90.431 | 0 | Video complete; MP2 could not initialize at this memory load. |
| MPEG-4 SP, 320x240 | 60/60 | 19.168/30 | 0 | 52.146 | 32.603 | 84.749 | 0 | Video complete; PCM could not initialize at this memory load. |
| Baseline H.263/AVI, 352x288 | 60/60 | 30.459/30 | 0 | 22.747 | 17.877 | 40.624 | 0 | Clean PCM; real time through the two-core pipeline. |
| BPV v7 streaming, 320x180 | 120/120 | 29.990/30 | 0 | 11.263 | 5.213 | 16.477 | 0 | Clean PCM; real time. |
| MPEG-1/MP2, 320x180 | 60/60 | 23.896/24 | 0 | 14.949 | 27.515 | 42.464 | 0 | Clean MP2; 40 audio frames decoded without underrun. |
| Baseline H.263/3GP, 352x288 | 60/60 | 20.303/30 | 0 | 32.671 | 16.787 | 49.457 | 0 | Legacy container video path complete; preserved test asset has no audio. |

The BPV v7 run averaged 25,011 compressed bytes and 9.4 decoder refill calls
per frame, physically exercising packets larger than both its 16 KiB stream
buffer and 4 KiB decoder refill without a frame gap. The successful 320x180
MPEG-1/MP2 run confirms that the MP2 path itself works; the 320x240 no-audio
result remains a profile memory limit rather than a general MP2 failure.

All regression clips except the legacy 3GP were already present before this
run. The 3GP was uploaded only as `CodexTest_H263_3GP.3gp`, then deleted by
that exact name. The original 43-byte `play.txt` was restored and reread with
CRC32 `ebbae2ae`; a fresh directory listing confirmed the test file absent and
preserved `play.txt`, `crc32.txt`, user and demo assets.
