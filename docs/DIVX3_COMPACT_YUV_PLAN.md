# DivX 3 compact YUV420 implementation plan

## Goal

Play the existing 320x240 DivX 3 profile on the ESP32-2432S028 without
PSRAM by reusing the Y6/U5/V5 frame representation already used by the HLV
and MPEG-1 decoders.

Physical-board flashing and measurements are intentionally excluded until
the board is available. Host tests and the ESP32 QEMU build are the current
acceptance environment.

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
- No physical board is flashed.
- Quality and QEMU performance results are recorded before the compact mode
  becomes the Player default.

## Known trade-off

DivX 3 normally uses full eight-bit decoded samples as future references.
Y6/U5/V5 references are therefore an embedded, non-bit-exact mode. The Q4
correction preserves each block's discarded average but cannot reconstruct
every lost low bit. The current GOP length of 12 bounds temporal drift by
resetting it at each I-frame; validation must still measure that drift across
the full GOP.
