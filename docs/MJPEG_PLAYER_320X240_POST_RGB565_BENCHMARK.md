# MJPEG Player 320x240 post-RGB565 benchmark

This result records the production Player after retaining the fixed-geometry
RGB565LE kernel and the packed exact U/V contribution tables.

## Test configuration

- date: 2026-07-26;
- firmware Git revision: `918e56c`;
- board: original ESP32-2432S028 at 240 MHz, without PSRAM;
- firmware: ordinary Player from the default ESP-IDF `build` directory;
- serial port: COM8 at 460800 baud;
- video: MJPEG, 320x240, 30/1 fps;
- audio: PCM, 16 kHz;
- collection: application reset followed by 3,000 consecutive `F` records;
- percentile method: nearest rank;
- frame budget: 33,333.333 us.

The raw frame records are preserved in
[`benchmarks/mjpeg_player_320x240_30fps_rgb565_2026-07-26.csv`](benchmarks/mjpeg_player_320x240_30fps_rgb565_2026-07-26.csv).
The file is 91,412 bytes and has SHA-256
`a3f7aa7f6b27ba4a2979af462ac5f0c5e2f0842e1a73c3a20b775c8cf49170a7`.

## Complete packet sequence

All 3,000 packet records are included here. Deliberately skipped late frames
have zero decode and render time, so these decode/render averages must not be
used as the latency of an actually decoded frame.

| Stage | Average, us | P50 | P95 | P99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| SD read | 9,727.8 | 7,365 | 15,609 | 21,946 | 23,568 |
| JPEG decode | 22,497.3 | 32,310 | 36,961 | 38,123 | 40,270 |
| Render | 1,035.6 | 1,432 | 1,947 | 2,151 | 3,041 |
| Complete work | 33,260.7 | 41,328 | 52,270 | 55,618 | 61,331 |
| Presentation | 23,579.8 | 33,869 | 38,509 | 39,733 | 115,121 |

The UART collector observed 29.999 packet records per second with no frame
sequence gaps.

## Actually decoded and displayed frames

The Player decoded and displayed 2,001 frames. The other 999 frames were
discarded before decode because they were already late. There were no frames
that were decoded but not rendered.

| Stage | Average, us | P50 | P95 | P99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| SD read | 9,087.6 | 7,301 | 14,928 | 20,978 | 23,568 |
| JPEG decode | 33,729.2 | 34,001 | 37,269 | 38,311 | 40,270 |
| Render | 1,552.6 | 1,444 | 2,064 | 2,177 | 3,041 |
| Complete work | 44,369.3 | 43,543 | 53,001 | 57,058 | 61,331 |
| Presentation | 35,340.8 | 35,577 | 38,942 | 39,935 | 115,121 |

- 778 of 2,001 decoded frames fit the decode-only 33.333 ms budget;
- 1,223 decoded frames, or 61.12%, exceeded that budget;
- 1,988 decoded frames, or 99.35%, exceeded the budget when SD read, decode
  and render work are summed;
- the effective displayed rate was approximately 20.010 fps;
- the skipped-presentation rate was 33.30%.

The stage sum does not fully describe wall-clock throughput because parts of
the Player pipeline overlap across tasks and cores. The displayed-frame count
is the authoritative end-to-end result.

## Comparison with the preceding production baseline

The preceding 900-packet production measurement used the same 320x240/30
profile. It decoded 565 packets, skipped 335, and reported the conditional
decode latency documented in
[`reverse_engineering/esp_new_jpeg_1.0.2/OPTIMIZATION_ANALYSIS.md`](reverse_engineering/esp_new_jpeg_1.0.2/OPTIMIZATION_ANALYSIS.md).

| Decode metric | Before, ms | After, ms | Change |
| --- | ---: | ---: | ---: |
| Average | 36.054 | 33.729 | -6.45% |
| P50 | 36.886 | 34.001 | -7.82% |
| P95 | 40.240 | 37.269 | -7.38% |
| P99 | 42.203 | 38.311 | -9.22% |

The displayed rate increased from approximately 18.833 to 20.010 fps
(+6.25%). The skipped-presentation rate fell from 37.22% to 33.30%, a
3.92-percentage-point reduction. The runs have different sample lengths, so
these end-to-end deltas are evidence of the production effect rather than a
cycle-exact A/B result. The isolated QEMU and COM8 A/B measurements remain the
acceptance evidence for the two retained kernels.

## Audio and acceptance status

The final audio record at frame 2,970 reported:

- queued bytes: 3,909;
- pending samples: 1,536;
- played samples: 1,584,128;
- rebuffers: 0;
- underrun samples: 0;
- inserted-silence chunks: 0;
- audio loop events and chunks: 0.

This is one successful 3,000-frame reset-to-reset run. It does not by itself
complete the three-run long-form acceptance criterion.
