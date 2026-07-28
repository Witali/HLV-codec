# ESP32 physical codec compatibility matrix

Date: 2026-07-28

## Test setup

- Board: ESP32-2432S028 with ESP32-D0WD-V3 revision 3.1, dual core at
  240 MHz.
- Connection: CH340C on COM8, UART telemetry at 460800 baud.
- Storage: the board's physical microSD card, mounted through the production
  SPI path.
- Baseline firmware: commit `66c69d7`.
- Fixed firmware: commit `b73623f`.
- Every timed run collected 120 consecutive frame records. A run that failed
  before the first frame was allowed to reach its normal player error screen.
- Audio underruns were recorded rather than treated as a reason to stop the
  capture.

`work` is the sum of the measured read, decode and render work. It can exceed
one frame period while two-core pipelining still maintains the source rate.
The verdict therefore also considers observed throughput, skipped display
submissions and audio telemetry. Times below are average / p95 in
milliseconds. A read time of `0` means that the codec path does not report
file reads separately, not that no SD access occurred.

## Baseline results for every supported codec

| Codec and profile | Test file | Result | Observed / source FPS | Read ms | Decode ms | Render ms | Work ms | Display skips | Audio and error |
|---|---|---|---:|---:|---:|---:|---:|---:|---|
| HLV v14, 320x180 | `Danila_320x180_30fps_HLVv14_49dB.hlv` | **DEGRADED** | 16.629 / 30 | 0 / 0 | 57.121 / 66.493 | 31.577 / 42.666 | 88.698 / 106.011 | 22/120 | Audio clean; decoder is too slow for real time. |
| HLV v14, 320x240 | `Danila_320x240_30fps_HLVv14_49dB.hlv` | **FAIL** | 0 / 30 | n/a | n/a | n/a | n/a | n/a | `Not enough RAM, use at most the 320x180 profile`; core decoder allocation failed. |
| BPV1 v5, 320x180 | `Danila_320x180_30fps_BPVv5_33dB.bpv1` | **PASS** | 30.104 / 30 | 9.731 / 12.994 | 9.746 / 10.061 | 13.803 / 14.465 | 33.280 / 36.830 | 0/120 | Audio clean. |
| BPV1 v7, 320x180 | `Danila_320x180_30fps_BPVv7_35dB.bpv1` | **PASS** | 30.104 / 30 | 0 / 0 | 11.646 / 13.021 | 4.961 / 6.051 | 16.607 / 17.739 | 0/120 | Audio clean. |
| BPV1 v7, 320x240 | `Danila_320x240_30fps_BPVv7_34dB.bpv1` | **PASS** | 30.104 / 30 | 0 / 0 | 15.427 / 17.389 | 6.179 / 7.605 | 21.606 / 23.044 | 0/120 | Audio clean. |
| DivX 3, 320x180 | `Danila_320x180_12fps_DivX3_42dB.avi` | **PASS** | 11.807 / 12 | 9.734 / 15.038 | 71.005 / 79.755 | 29.869 / 31.620 | 110.608 / 122.974 | 2/120 | Audio clean. |
| DivX 3, 320x240 | `Danila_320x240_12fps_DivX3_42dB.avi` | **FAIL** | 0 / 12 | n/a | n/a | n/a | n/a | n/a | `Not enough RAM, use at most the 320x240 DivX 3 profile`; the message is misleading because this file is already 320x240. |
| H.263+, 320x180 | `Danila_320x180_15fps_H263p_37dB.avi` | **PASS** | 14.991 / 15 | 0 / 0 | 35.148 / 43.486 | 14.831 / 15.945 | 49.980 / 58.504 | 0/120 | Audio clean. |
| H.263+, 320x240 | `Danila_320x240_15fps_H263p_36dB.avi` | **PASS** | 15.021 / 15 | 0 / 0 | 36.931 / 53.757 | 19.377 / 20.387 | 56.307 / 73.409 | 0/120 | Audio clean. |
| Baseline H.263 CIF, 352x288 | `Danila_352x288_30fps_H263_28dB.avi` | **PASS** | 30.096 / 30 | 0 / 0 | 16.197 / 18.383 | 19.336 / 20.687 | 35.533 / 38.185 | 2/120 | Audio clean; the central 320x240 region is displayed. |
| MJPEG/AVI, 320x180 | `Danila_320x180_30fps_MJPEG_43dB.avi` | **FAIL** | 0 / 30 | n/a | n/a | n/a | n/a | n/a | `JPEG_DEC: Resolution(180*320) is not times of 8`; `esp_new_jpeg header failed`. |
| MJPEG/AVI, 320x240 | `Danila_320x240_30fps_MJPEG_43dB.avi` | **DEGRADED** | 29.869 / 30 | 11.590 / 18.015 | 21.107 / 40.614 | 0.882 / 2.033 | 33.578 / 55.409 | 53/120 | Audio clean, but only 67 of 120 frames were submitted to the display. |
| MPEG-1/PS, 320x180 | `Danila_320x180_30fps_MPEG1_44dB.mpg` | **DEGRADED** | 15.513 / 30 | 0 / 0 | 60.841 / 73.284 | 29.718 / 84.462 | 90.559 / 147.261 | 28/120 | MP2 decode 17.829 ms/frame; one rebuffer, 4096 underrun samples and 16 silence chunks. |
| MPEG-1/PS, 320x240 | `Danila_320x240_30fps_MPEG1_44dB.mpg` | **FAIL** | 0 / 30 | n/a | n/a | n/a | n/a | n/a | Valid 320x240 header, but `plm_get_duration()` returned -1 and the player reported `Invalid video.mpg`. |

No legacy `.3gp` asset was present on the card or in `out`, so the legacy
H.263/3GP container path was not exercised. Project encoding rules prohibit
creating a new 3GP target; baseline H.263 in the preferred AVI container was
tested at CIF instead.

## Verification after the fixes

| Codec and profile | Result | Observed / source FPS | Read ms | Decode ms | Render ms | Work ms | Display skips | Audio and notes |
|---|---|---:|---:|---:|---:|---:|---:|---|
| MJPEG/AVI, 320x180 | **DEGRADED, now opens** | 29.982 / 30 | 9.284 / 15.489 | 23.006 / 31.913 | 0.985 / 1.536 | 33.274 / 45.899 | 26/120 | Audio clean. All 120 packets decoded without a frame gap; 94 were submitted to the LCD. |
| MJPEG/AVI, 320x240 | **DEGRADED** | 29.869 / 30 | 11.596 / 18.003 | 21.094 / 40.550 | 0.897 / 1.821 | 33.587 / 55.427 | 53/120 | Audio clean; unchanged throughput limitation. |
| MPEG-1/PS, 320x180 | **DEGRADED** | 15.543 / 30 | 0 / 0 | 61.100 / 74.308 | 29.485 / 83.432 | 90.585 / 141.613 | 28/120 | MP2 decode 17.975 ms/frame; one rebuffer, 4096 underrun samples and 16 silence chunks. |
| MPEG-1/PS, 320x240 | **DEGRADED, now opens** | 12.953 / 30 | 0 / 0 | 77.115 / 90.677 | 38.127 / 38.341 | 115.242 / 128.818 | 0/120 | All 120 frames rendered. MP2 audio initialization fails because internal RAM is exhausted, so playback uses the timer clock without sound. |
| BPV1 v7, 320x240 control | **PASS** | 29.982 / 30 | 0 / 0 | 16.068 / 17.811 | 6.351 / 7.830 | 22.420 / 24.022 | 0/120 | Audio clean; no regression. |
| Baseline H.263 CIF control | **PASS** | 30.104 / 30 | 0 / 0 | 16.283 / 18.517 | 18.756 / 20.074 | 35.039 / 37.568 | 2/120 | Audio clean; no regression. |

The MPEG-1 320x240 audio diagnosis was repeated twice. Releasing the optional
10,240-byte secondary LCD DMA buffer did not make MP2 initialization succeed
and increased render time from about 38.1 to 39.7 ms, so that experiment was
discarded.

## Fixes and remaining limitations

The MJPEG fix only adjusts the JPEG SOF height used internally by the Espressif
decoder when the AVI's visible height is not divisible by eight. It first
checks that the padded height does not add an MCU row, decodes into the
existing 16-row DMA strip, and sends only the original visible rows to the
screen. It adds no frame-sized allocation.

The MPEG-1 fix removes the full-file duration scan from the sequential player.
Duration is not required to decode a program stream until EOF, and some valid
SD-backed streams cannot provide it through `pl_mpeg`. Width, height and frame
rate remain validated.

These are compatibility fixes, not claims of 30 FPS decoding:

- BPV1 v5/v7 and baseline H.263 CIF are the tested real-time 30 FPS paths.
- H.263+ and DivX 3 meet the lower source rates of the tested files.
- MJPEG now accepts the 320x180 file, but both MJPEG profiles still skip LCD
  submissions under load.
- MPEG-1 is substantially slower than real time at both resolutions.
- HLV 320x240, DivX 3 320x240 and MPEG-1 320x240 with MP2 audio still have
  internal-RAM limitations.
