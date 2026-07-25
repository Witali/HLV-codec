# DivX 3 compact YUV420 implementation plan

## Goal

Play the existing 320x240 DivX 3 profile on the ESP32-2432S028 without
PSRAM by reusing the Y6/U5/V5 frame representation already used by the HLV
and MPEG-1 decoders.

The initial acceptance environment was the host and ESP32 QEMU. Physical-board
measurements were added after the board became available.

## Compact frame representation

- Store luma as rounded six-bit samples packed within each row.
- Store both chroma planes as rounded five-bit samples packed within each
  row.
- Store one signed Q4 average-error correction for every 8x8 plane block.
- Apply the correction with the existing deterministic 4x4 threshold map.
- Keep codec-specific half-pixel interpolation and rounding in each decoder.

At 320x240 a normal YUV420 frame occupies 115,200 bytes. A compact frame
occupies 81,600 bytes plus 1,800 correction bytes, or 83,400 bytes. Two
reference frames therefore shrink from 230,400 to 166,800 bytes.

## Implementation stages

1. Add a codec-independent compact-YUV420 module.
   - Define non-owning plane and frame descriptors.
   - Centralize layout calculation, quantization, Q4 correction, packed
     sample reads, row unpacking, block storage, block copy and block fill.
   - Add exhaustive unit tests for packing, correction ranges, edge values,
     row addressing and copy behavior.
2. Migrate HLV and MPEG-1 to the common arithmetic.
   - Preserve their public frame structures through small adapters.
   - Verify byte-identical compact output and unchanged decoded output on
     existing regression clips.
3. Add an optional compact-reference mode to the portable DivX 3 decoder.
   - Keep ordinary eight-bit output as the default reference implementation.
   - Decode one 8x8 block at a time and write it directly to compact storage.
   - Read motion-compensation samples through the common corrected-sample
     accessor.
   - Copy zero-motion skipped blocks in packed form.
4. Replace full-picture DivX predictor grids with rolling top-row arrays and
   left-neighbor state.
   - Cover DC, AC, coded-block and motion-vector prediction.
   - Verify exact-mode output remains byte-identical before enabling compact
     mode in firmware.
5. Integrate compact DivX output into the ESP32 Player.
   - Enable the mode only for the embedded Player.
   - Raise the accepted profile to 320x240.
   - Render compact planes without allocating a full unpacked frame.
   - Retain the host decoder's exact eight-bit mode.
6. Validate without hardware.
   - Run the compact-YUV unit tests.
   - Decode complete DivX files in exact and compact modes.
   - Track per-frame MSE/PSNR and error versus distance from the I-frame.
   - Verify both supplied 320x240 files decode to completion.
   - Build the ESP-IDF firmware and run the relevant QEMU benchmark.
   - Record decoder memory, peak packet size and QEMU timing.

## Acceptance criteria

- Existing HLV and MPEG-1 regression tests continue to pass.
- Exact DivX mode remains byte-identical to its pre-change output.
- Compact DivX mode completes both supplied 320x240 videos without decode
  errors.
- Compact mode uses no more than 175 KiB for decoder-owned frame and
  predictor storage at 320x240.
- The firmware accepts 320x240 DivX 3 files and builds successfully.
- Physical-board validation is deferred until hardware becomes available.
- Quality and QEMU performance results are recorded before the compact mode
  becomes the Player default.

## Known trade-off

DivX 3 normally uses full eight-bit decoded samples as future references.
Y6/U5/V5 references are therefore an embedded, non-bit-exact mode. The Q4
correction preserves each block's discarded average but cannot reconstruct
every lost low bit. The current GOP length of 12 bounds temporal drift by
resetting it at each I-frame; validation must still measure that drift across
the full GOP.

## Completed validation

All implementation stages above are complete. The initial validation was
performed on the host and in QEMU before a board became available.

- The common compact-YUV420 unit test passes.
- HLV v12 host simulation remains byte-identical to `main`
  (`c1c42ad1ab6c76c9` reconstruction hash).
- MPEG-1 exact and compact regression hashes remain byte-identical to `main`
  (`daf2015f46b89d2e` and `b4f0a2fe777fefc3`).
- Exact DivX 3 output remains byte-identical before and after rolling
  predictors. Full-file hashes are `dfa88cf6db4bd23b` for Big Buck Bunny
  (7,158 frames) and `5e24bc69af1d82cf` for VID_20260522_181611
  (1,343 frames).
- Compact DivX 3 decodes both complete QVGA files. Their hashes are
  `bc690c13d270e921` and `6051d1ab5b132596`, respectively.
- Compact-versus-exact PSNR is 43.06 dB overall for Big Buck Bunny and
  42.11 dB for VID_20260522_181611. The worst individual frames are
  36.10 dB and 36.62 dB. Mean signed error is below 0.015 sample in
  magnitude for both files.
- On a 64-bit host, QVGA decoder-owned memory is 174,000 bytes in compact
  mode and 237,600 bytes in exact mode. The original exact implementation
  used 293,576 bytes, so rolling predictors plus compact references save
  119,568 bytes (40.7%).
- The normal ESP-IDF Player build succeeds. Its application image is
  647,136 bytes and leaves 925,728 bytes free in the 1.5 MiB application
  partition.
- Espressif QEMU decodes the validated 60-frame 320x240 clip successfully.
  The compact decoder is 173,916 bytes with 32-bit pointers. Average,
  p50, p95 and maximum decode costs are 3,348,621, 3,512,118,
  4,484,926 and 4,835,351 guest cycles. The decode-only rate calculated
  at 240 MHz is 71.671 fps.
- QEMU and the host compact decoder produce the same visible-frame hash,
  `1463ec78314286a3`, for that clip.

The Player selects compact DivX 3 storage, accepts at most 320x240, uses one
10 KiB RGB565 display strip allocation and reduces DivX stdio read-ahead from
16 KiB to 4 KiB.

## Physical-board validation

The original contiguous allocation for both compact reference frames failed
before the first picture on the ESP32. Allocating the two frames independently
reduces the largest request from 133,440 to 66,720 bytes at 320x180 and from
166,800 to 83,400 bytes at 320x240. Both profiles then decode successfully.

A 300-frame ESP32-D0WD-V3 run at 240 MHz produced:

- 320x180: zero sequence gaps, rebuffers, underrun samples or silence chunks;
  89.3 ms average and 167.0 ms p95 decode time, 8.06 observed fps, 57 display
  skips and 23 audio-loop events.
- 320x240: zero sequence gaps, rebuffers, underrun samples or silence chunks;
  111.1 ms average and 197.5 ms p95 decode time, 6.64 observed fps, 72 display
  skips and 26 audio-loop events.

Independent frame allocations therefore solve the memory blocker without
changing reconstructed pixels, but the current q4 material does not sustain
the saved 12 fps profile on the physical board. CPU optimization or a lower
frame rate is still required for smooth playback.
