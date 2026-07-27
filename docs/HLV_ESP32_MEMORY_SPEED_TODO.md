# HLV ESP32 memory and unpack performance TODO

This checklist tracks bit-exact HLV v14 decoder and player experiments for the
ESP32-2432S028. The primary goals are to make the `320x240` compact decoder fit
in internal RAM and to increase packed Y7/U6/V6 decode/render throughput.

## Acceptance rules

Every retained experiment must:

1. preserve complete decoded-frame hashes in native and Xtensa QEMU tests;
2. keep packet CRC verification enabled;
3. consume compressed input through a fixed-capacity streaming buffer;
4. decode a valid packet larger than that buffer with the same frame hash as
   the contiguous-input path;
5. report heap use, largest free block, code size and IRAM change;
6. improve the relevant physical-board median by at least `0.5%`, unless it is
   required to make `320x240` open;
7. be committed separately after validation;
8. remove rejected source changes and record their measured result here.

Run at least three decoder-only physical measurements for speed decisions.
End-to-end acceptance uses at least 300 consecutive frame records with no
sequence gaps. Audio-enabled final acceptance requires no rebuffers, missing
samples or inserted silence.

## Current baseline

Physical ESP32, HLV v14 `320x180`, 30 fps, high-quality 49 dB asset:

| Metric | Average | P95 | Maximum |
| --- | ---: | ---: | ---: |
| Decode | 55.602 ms | 63.694 ms | 72.245 ms |
| Render | 30.869 ms | 39.489 ms | 41.181 ms |
| Work | 86.470 ms | 100.489 ms | 108.126 ms |

- Observed rate: `16.935 fps`.
- Frames: `300`, sequence gaps: `0`, display skips: `54`.
- Audio: zero rebuffers, underrun samples and silence chunks.
- HLV v14 `320x240` fails before frame 1 with
  `Core decoder allocation failed`.

Compact decoder storage, including two references, Q4 corrections and unpacked
working rows:

| Picture | Y7/U6/V6 bytes |
| --- | ---: |
| `320x180` padded to `320x192` | 164,160 |
| `320x240` | 203,280 |

The previous Y6/U5/V5 `320x240` layout used 174,480 bytes. Increasing the
reference precision therefore added 28,800 bytes. Y7/U6/V6 is normative for
HLV v14 and must not be reduced only in the decoder.

Current local HLV v14 38 dB syntax profile:

| Picture | Copy samples/frame | H/V interpolation | 2D interpolation | Coefficients | WHT blocks |
| --- | ---: | ---: | ---: | ---: | ---: |
| `320x180` | 42,970 | 29,225 | 14,821 | 20,815 | 2,781 |
| `320x240` | 50,140 | 37,275 | 20,806 | 24,793 | 3,392 |

## Phase 0: reproducible measurements

- [ ] Record current native compact hashes and timing for both picture sizes.
- [x] Record current Xtensa QEMU cycles, size and heap for representative GOP
      windows from both files.
- [ ] Port the opt-in stage profiler from commit `3f541bf` to the current C99
      Y7/U6/V6 decoder. Keep every timer read compiled out by default.
- [x] Add allocation-stage heap logging for `ESP_PLATFORM`, including the
      largest 8-bit and DMA-capable blocks.
- [ ] Record three synchronized physical baselines at UART `460800`.

## Phase 1: reduce RAM and open 320x240

- [ ] Add a one-reference HLV fast path modelled on BPV v7:
  - enforce and validate the header motion-search radius for every actual
    global, macroblock, subblock and rectangular motion vector;
  - initially support a maximum eight-pixel vertical radius;
  - reconstruct into bounded rows and delay replacement of old reference rows
    until no future destination row can reach them;
  - keep only the current top-neighbour state required by INTRA prediction;
  - guard replacement against CPU0 render progress so CPU1 decode can continue
    in parallel without overwriting an unrendered old row;
  - use the existing two-reference decoder as a compatibility fallback when
    memory permits, and reject a falsely declared radius rather than corrupting
    the reference.
- [ ] Measure the one-reference target at `320x240`: one 97,800-byte packed
      Y7/U6/V6+Q4 reference plus bounded reconstruction/commit rows, instead
      of two references.
- [ ] Re-evaluate use of only the primary LCD buffer after the one-reference
      path fits. The isolated attempt saved 10,240 bytes but made 320x180
      render average 1.78% slower and still did not open 320x240.
- [ ] Replace HLV's 16 KiB stdio read-ahead plus decoder refill allocation with
      one fixed-capacity asynchronous input ring/refill path. Start with an
      8 KiB ring and a 4 KiB SD read chunk.
- [ ] Ensure that only the reader owns the video file and that compressed data
      is never copied between decode tasks.
- [ ] Test a valid packet larger than the ring and compare its decoded hash
      with contiguous and direct-file decode.
- [ ] Reorder large allocations only if heap logs show fragmentation rather
      than insufficient total capacity.
- [ ] Reduce the full-width 7,680-byte unpacked working rows to bounded
      macroblock/4x4 storage as part of fused reconstruction.
- [ ] Confirm that HLV `320x240` opens with audio enabled and record free heap,
      largest block and stack high-water marks.

## Phase 2: packed prediction and reconstruction

- [x] In vertical and bilinear packed prediction, reuse the previous bottom
      source row as the next top row instead of unpacking it twice.
- [ ] Split integer, horizontal, vertical and bilinear packed prediction into
      fixed-denominator Y7/U6/V6 kernels so the compiler can fully specialise
      the hot loops.
- [ ] Evaluate selective IRAM placement for only the retained packed prediction
      kernels.
- [ ] Fuse motion prediction, 4x4 residual addition, Y7/U6/V6 packing and Q4
      correction accumulation. Avoid a complete unpacked macroblock pass.
- [ ] Re-evaluate aligned no-residual packed motion only inside the fused
      implementation; do not restore the previously rejected standalone copy.

## Phase 3: packing, entropy and transform

- [ ] Add an unrolled byte-aligned eight-sample Y7 pack path. U6 already uses
      the existing specialised six-bit path.
- [ ] Amortise Q4 correction setup over each 8-pixel span and test a four-phase
      correction pattern against the current per-sample threshold comparison.
- [ ] Profile coefficient counts above two. Add another sparse transform path
      only for a frequent count whose physical gain exceeds `0.5%`.
- [ ] Evaluate a fully unrolled C inverse-WHT before writing Xtensa assembly.
- [ ] Retain an Xtensa inverse-WHT kernel only when the complete decoder hash
      is unchanged and the physical median improves by at least `0.5%`.
- [ ] Test ESP32 ROM CRC32 and a bounded word-at-a-time implementation. CRC was
      historically about 1.4 ms/frame, so it is not a primary target.

## Phase 4: display unpack and RGB565

- [ ] Cache each corrected U6/V6 row across its two Y7 luma rows.
- [ ] Fuse aligned Y7 unpack, Q4 correction and RGB565 conversion without an
      intermediate full luma row.
- [ ] Precompute corrected U/V colour contributions once per chroma sample and
      reuse them for both associated luma rows.
- [ ] Measure conversion CPU cycles separately from LCD DMA waits.

## Phase 5: encoder-side decode cost

- [ ] Compare `--decode-cycle-weight` values `0`, `0.01`, `0.025` and `0.05`
      at matched PSNR/SSIM.
- [ ] Record bitrate, maximum packet size, prediction mix, coefficient/WHT
      counts, physical decode time and render time.
- [ ] Add a separate ESP32-oriented encoding profile only if it preserves the
      requested quality while producing a clear end-to-end improvement.

## Do not repeat unchanged

These experiments were already below the acceptance threshold or regressed:

- decoder-wide `-O2` and `-Os`;
- aligned 32-bit empty-cache refill;
- standalone direct packed INTER/GLOBAL copy;
- destructive residual-mask shifting;
- branchless WHT rounding;
- branchless compact quantisation;
- direct compact PALETTE;
- direct zero-residual INTRA;
- duplicated version-specific mode parsing.

## Results

| Experiment | Native | QEMU | Physical ESP32 | RAM | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Current Y7/U6/V6 baseline | pending | 2,959,678 cycles at 320x180; 3,696,921 at 320x240 | 55.602 ms decode at 320x180 | 203,280-byte 320x240 core | baseline |
| Stronger QEMU frame hash and large-packet coverage | pending | hashes `150eef90ad52b1ae` at 320x180 and `612a072f0b034761` at 320x240; packets 46,652 and 58,551 bytes through a 7,680-byte refill | not applicable | no player change | retained |
| Primary-only LCD DMA storage | not applicable | not applicable | render 30.869 -> 31.419 ms (+1.78%); 320x240 still failed | -10,240 bytes | rejected in isolation |
| Allocate both compact luma planes before chroma | unchanged hash | unchanged hash | second 320x240 frame allocation still failed with a 13,312-byte largest block for a 14,400-byte V plane | unchanged | rejected |
| Reuse packed vertical/bilinear source row | pending | 2,622,911 cycles at 320x180 (-11.38%); 3,255,409 at 320x240 (-11.94%); hashes unchanged | three-run median 54.766 ms decode (-1.50%), about 0.88% lower complete work | unchanged | retained |
