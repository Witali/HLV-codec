# ESP32 SD-card maximum-resolution decoder speed — 2026-08-02

## Setup and selection

- Firmware: primary C99 ESP-IDF player at commit `6380264`, built with the
  tracked `-O3` configuration and `MPEG1_RENDER_PROFILE=OFF`.
- Board: ESP32-2432S028, ESP32-D0WD-V3 revision 3.1 at 240 MHz, COM8 at
  460800 baud, physical microSD production path.
- Corpus: all 35 files at the largest resolution available for their codec:
  320x240 for HLV, BPV, MJPEG, MPEG-1, MPEG-4 SP and DivX 3; 352x288 for
  baseline H.263. Fifteen 320x180 files were omitted as lower-resolution
  analogues.
- Window: the first 60 consecutive frames, except the complete 30-frame DivX
  regression clip. Missing audio or an audio underrun did not invalidate a
  decoder-speed run. Frame gaps and player errors did.

`Decode fps` is decode-only throughput calculated from average decode time. It
does not include SD time reported separately, RGB565 conversion or LCD work.
`Observed/source fps` is the complete two-core player cadence. A display skip
means the predictive frame was decoded but a late LCD submission was omitted.

## Results

| Codec | SD file | Decode ms | Decode fps | Observed/source fps | Render ms | Display skips |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| DivX 3 | `BigBuckBunny_320x240_12fps_DivX3_41dB.avi` | 31.542 | 31.704 | 11.987/12 | 34.796 | 0 |
| DivX 3 | `Danila_320x240_12fps_DivX3_42dB.avi` | 102.383 | 9.767 | 9.783/12 | 30.460 | 8 |
| DivX 3 | `Danila_320x240_15fps_DivX3_40dB.avi` | 84.502 | 11.834 | 11.655/15 | 28.579 | 11 |
| DivX 3 | `VideoFormatRegression_320x240_15fps_DivX3_q3.avi` | 54.755 | 18.263 | 14.972/15 | 35.969 | 0 |
| BPV v5 | `BigBuckBunny_320x240_24fps_BPVv5_35dB.bpv1` | 4.359 | 229.426 | 24.052/24 | 16.410 | 0 |
| BPV v5 | `BigBuckBunny_320x240_24fps_BPVv5_36dB.bpv1` | 4.848 | 206.262 | 23.896/24 | 16.925 | 0 |
| BPV v5 | `Danila_320x240_30fps_BPVv5_33dB.bpv1` | 13.200 | 75.756 | 29.964/30 | 16.673 | 0 |
| BPV v6 | `VideoFormatRegression_320x240_30fps_BPVv6_35dB.bpv1` | 5.568 | 179.598 | 29.964/30 | 16.869 | 0 |
| BPV v7 | `BigBuckBunny_320x240_24fps_BPVv7_36dB.bpv1` | 7.544 | 132.554 | 23.906/24 | 12.478 | 0 |
| BPV v7 | `Danila_320x240_30fps_BPVv7_34dB.bpv1` | 15.549 | 64.312 | 30.210/30 | 6.159 | 0 |
| HLV v14 | `BigBuckBunny_320x240_24fps_HLVv14_41dB.hlv` | 42.667 | 23.438 | 24.052/24 | 37.200 | 0 |
| HLV v14 | `BigBuckBunny_320x240_24fps_HLVv14_49dB.hlv` | 42.677 | 23.432 | 23.896/24 | 37.322 | 1 |
| HLV v14 | `Danila_320x240_30fps_HLVv14_38dB.hlv` | 90.279 | 11.077 | 11.170/30 | 30.574 | 10 |
| HLV v14 | `Danila_320x240_30fps_HLVv14_49dB.hlv` | 65.539 | 15.258 | 15.477/30 | 31.678 | 12 |
| HLV v14 | `VideoFormatRegression_320x240_30fps_HLVv14_adaptive35-42dB.hlv` | 52.554 | 19.028 | 19.168/30 | 30.704 | 10 |
| MJPEG | `BigBuckBunny_320x240_24fps_MJPEG_40dB.avi` | **SKIP** | — | — | — | — |
| MJPEG | `Danila_320x240_30fps_MJPEG_40dB.avi` | **SKIP** | — | — | — | — |
| MJPEG | `Danila_320x240_30fps_MJPEG_43dB.avi` | 29.859 | 33.491 | 23.600/30 | 0.755 | 30 |
| MJPEG | `VideoFormatRegression_320x240_30fps_MJPEG_q3.avi` | 23.310 | 42.900 | 29.738/30 | 0.817 | 29 |
| MPEG-1 | `BigBuckBunny_320x240_24fps_MPEG1_40dB.mpg` | 16.486 | 60.659 | 24.200/24 | 31.939 | 0 |
| MPEG-1 | `Danila_320x240_30fps_MPEG1_41dB.mpg` | 71.746 | 13.938 | 13.834/30 | 32.590 | 0 |
| MPEG-1 | `Danila_320x240_30fps_MPEG1_44dB.mpg` | 86.491 | 11.562 | 11.548/30 | 32.664 | 0 |
| MPEG-1 | `VideoFormatRegression_320x240_30fps_MPEG1_q3.mpg` | 57.874 | 17.279 | 17.241/30 | 32.554 | 0 |
| MPEG-4 SP | `BigBuckBunny_320x240_24fps_MPEG4SP_35dB.avi` | 21.264 | 47.027 | 24.370/24 | 31.886 | 0 |
| MPEG-4 SP | `Danila_320x240_30fps_MPEG4SP_35dB.avi` | 66.220 | 15.101 | 15.043/30 | 32.629 | 0 |
| MPEG-4 SP | `Danila_320x240_30fps_MPEG4SP_SPEED_q7.avi` | 60.643 | 16.490 | 16.490/30 | 32.882 | 0 |
| MPEG-4 SP | `VideoFormatRegression_320x240_30fps_MPEG4SP_35dB.avi` | 51.667 | 19.355 | 19.363/30 | 32.600 | 0 |
| H.263 | `BigBuckBunny_352x288_24fps_5min_H263_CIF_q6.avi` | 8.067 | 123.968 | 24.042/24 | 17.941 | 0 |
| H.263 | `BigBuckBunny_352x288_24fps_H263_36dB.avi` | 11.545 | 86.618 | 24.052/24 | 17.964 | 0 |
| H.263 | `BigBuckBunny_352x288_24fps_H263_39dB.avi` | 13.236 | 75.549 | 24.200/24 | 17.994 | 0 |
| H.263 | `Danila_352x288_30fps_H263_28dB.avi` | 16.759 | 59.668 | 29.723/30 | 17.511 | 2 |
| H.263 | `Danila_352x288_30fps_H263_29dB.avi` | 21.944 | 45.570 | 27.164/30 | 17.468 | 2 |
| H.263 | `Danila_352x288_30fps_H263_36dB_q7.avi` | 31.959 | 31.290 | 29.723/30 | 17.765 | 0 |
| H.263 | `Danila_352x288_30fps_H263_Q6.avi` | 34.917 | 28.639 | 24.360/30 | 16.599 | 4 |
| H.263 | `VideoFormatRegression_352x288_30fps_H263_CIF_q6.avi` | 22.724 | 44.006 | 30.210/30 | 17.878 | 0 |

The two skipped MJPEG files repeatedly produced
`Invalid video.avi: unsupported AVI/MJPEG format` before the first frame. Per
the test rule, they were not allowed to stop the remaining corpus.

## Summary

- BPV v5-v7 is the fastest and every tested BPV file maintains its source
  cadence with zero display skips.
- H.263 CIF decode ranges from 8.067 to 34.917 ms. Six of eight files are at
  or close to source cadence; the 29 dB and Q6 Danila files fall below 30 fps.
- MJPEG decode itself is 23.310-29.859 ms, but both valid files omit about half
  of their LCD submissions under the current A/V policy.
- HLV, DivX 3, MPEG-1 and MPEG-4 SP vary materially with content. Their Big
  Buck Bunny profiles maintain their lower source rates, while most 30 fps
  Danila and regression profiles do not.

## SD cleanup

No video was uploaded or generated for this run; only the persistent
`play.txt` selection changed. Its original 43-byte contents were restored and
reread with CRC32 `ebbae2ae` and SHA256
`45F047E31CEA3CF7C2DB8A0C4D66ED2EBE1919BC898504FEDF0BB52D41C6BE32`. A fresh
directory listing confirmed all 50 original videos, `play.txt` and
`crc32.txt` present and no test-only files.
