# ESP32 multi-codec benchmark: VID_20260522_181611

Date: 2026-07-25

> This is the result for firmware `cc01cc6`. The later bounded-memory HLV
> streaming reader replaces the 69,120-byte packet pool with two 7680-byte
> windows. The same HLV 320x240 file now opens and plays with audio; a
> 300-frame hardware sample reached 9.362 fps. The RAM failure below remains
> useful as the baseline, but is no longer the current HLV startup result.

## Test setup

- Board: ESP32-2432S028, real hardware on COM8.
- Firmware: commit `cc01cc6`, ESP-IDF player, SD SPI at 40 MHz.
- Source: `out/sources/VID_20260522_181611.mp4`.
- Picture: centre-cropped from 16:9 to 4:3, then scaled to 320x240.
- Frame rate: native 30/1 fps.
- Display scaling: disabled; every test frame is already 320x240.
- Timing budget: 33,333 us per frame.
- Audio is enabled and used as the playback clock when its initialization
  succeeds.
- Every complete frame record was captured over UART. The host-side capture
  tool is commit `46f4420`.

The `work_us` field is the sum of SD, decode and render work. It may exceed one
frame period without reducing throughput when stages overlap on the two CPU
cores. Therefore the pass/fail decision uses observed fps, display skips and
audio telemetry, not `work_us` alone.

## Encoded files

| Codec | Local file | Size | Video | Audio |
|---|---|---:|---|---|
| HLV v13 | `out/VID_20260522_181611.hlv` | 82,474,306 B | adaptive 35-42 dB, 3359 frames | PCM_U8 mono 16 kHz |
| BPV1 v4 | `out/VID_20260522_181611_v4.bpv1` | 145,392,123 B | active palettes, lambda 0, GOP 48, 3359 frames | PCM_U8 mono 16 kHz |
| MJPEG/AVI | `out/VID_20260522_181611_center-crop_320x240_mjpeg_q5_native-fps.avi` | 71,160,402 B | q=5, 3357 frames | PCM_U8 mono 16 kHz |
| MPEG-1/PS | `out/VID_20260522_181611_center-crop_320x240_mpeg1_q3_native-fps.mpg` | 63,242,240 B | q=3, GOP 30, no B pictures, 3359 frames | MP2 mono 32 kHz, 64 kbit/s |

Both newly encoded standard files were completely decoded with FFmpeg before
upload. The MPEG-1 encoder also inspected every picture type and found no B
pictures. UART upload completed with device-side CRC32 `b68ec3d6` for MJPEG
and `84810996` for MPEG-1.

## Hardware results

| Codec | Frames | Observed fps | SD avg / p95 / max | Decode avg / p95 / max | Render avg / p95 / max | Display skips | Audio result | Verdict |
|---|---:|---:|---:|---:|---:|---:|---|---|
| HLV v13 | 0 | n/a | n/a | n/a | n/a | n/a | not started | **FAIL: insufficient RAM at 320x240** |
| BPV1 v4 | 3359/3359 | 30.003 | 16.820 / 19.225 / 22.910 ms | 5.115 / 5.345 / 5.400 ms | 16.837 / 17.256 / 20.082 ms | 0 | no rebuffer, underrun, silence or loop | **PASS** |
| MJPEG | 3357/3357 decoded | 15.995 | 9.486 / 15.623 / 26.154 ms | 50.572 / 89.714 / 98.709 ms | 1.120 / 2.115 / 2.868 ms | 1311 | no sample loss, but 271 holds and 6123 repeated DMA chunks | **FAIL: video and A/V timing** |
| MPEG-1 | 3359/3359 decoded | 12.395 | not isolated by the pl_mpeg path | 80.611 / 99.587 / 111.819 ms | 52.762 / 53.025 / 53.492 ms | 0 | initialization failed; no audio telemetry or DAC clock | **FAIL: video and audio** |

Times in the table are average / p95 / maximum.

### HLV

The decoder fails before the first frame:

```text
Not enough RAM, use at most the 320x180 profile
Packet block 5/9 failed: heap=7156 largest=5376
```

This 320x240 HLV file is valid, but the current firmware cannot allocate its
decoder and packet pool together with the other player buffers. No correct
video or audio playback occurs.

### BPV1

BPV1 is the only tested 320x240 codec that sustains the native frame rate and
keeps audio continuous. Its average `work_us` is 38.773 ms and 3332 records
exceed 33.333 ms as a sum, but the stages overlap across the two cores:
observed throughput remains 30.003 fps with zero skipped presentations.

The largest summed work record is frame 1693 at 45.372 ms. The largest
`present_us` is the startup frame at 96.211 ms; p95 is 47.394 ms. There are no
frame sequence gaps and no audio error or hold events.

Full per-frame data:
`out/VID_20260522_181611_bpv1_esp32_timings.csv`.

### MJPEG

MJPEG decoding is content-dependent and bimodal. Decode p50 is 78.377 ms and
p95 is 89.714 ms, so the decoder cannot meet the 33.333 ms deadline for most
non-skipped frames. The player omits 1311 display transfers to limit lateness,
but still reaches only 15.995 fps.

The largest summed work record is frame 1815 at 117.541 ms. Maximum
`present_us` is the startup frame at 166.234 ms; p95 is 101.525 ms. Audio
samples are not reported as lost, but repeating 6123 DMA chunks during 271
hold events makes the audible timing incorrect.

Full per-frame data:
`out/VID_20260522_181611_mjpeg_esp32_timings.csv`.

### MPEG-1

The pipelined MPEG-1 decoder renders every decoded frame instead of dropping
late frames, so playback stretches to 12.395 fps. Every one of the 3359
`work_us` records exceeds the frame budget. Decode is the limiting stage; the
largest summed work is frame 659 at 164.855 ms. `present_us` p95 is
53.029 ms and its maximum is 53.496 ms.

The stream contains a valid 32 kHz MP2 track and the player reports that sample
rate in its video header. However, audio telemetry is emitted every 30 frames
only while `audio_enabled` is true, and no audio record was emitted during the
complete run. The code path therefore failed `prepareAudio()`, stopped the
audio resources and continued on the timer clock. The MPEG-1 result has no
working audio on this firmware.

Full per-frame data:
`out/VID_20260522_181611_mpeg1_esp32_timings.csv`.

## Conclusion

At 320x240 and 30 fps, **BPV1 v4 is the only tested format that currently
plays both video and audio correctly in real time**.

- HLV needs a smaller 320x180 profile or a memory-layout reduction before it
  can open at 320x240.
- MJPEG needs a substantially faster JPEG decode/render schedule; its current
  result is about 16 fps and forces audible hold events.
- MPEG-1 needs both decoder acceleration and enough memory/resources for the
  MP2 audio path. Its current video rate is about 12.4 fps and audio does not
  start.

After testing, `/HLV/play.txt` was restored to
`VID_20260522_181611_v4.bpv1`.
