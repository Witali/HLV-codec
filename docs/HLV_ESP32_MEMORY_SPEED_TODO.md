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
- [ ] Record current Xtensa QEMU cycles, size and heap for representative GOP
      windows from both files.
- [ ] Port the opt-in stage profiler from commit `3f541bf` to the current C99
      Y7/U6/V6 decoder. Keep every timer read compiled out by default.
- [ ] Add allocation-stage heap logging for `ESP_PLATFORM`, including the
      largest 8-bit and DMA-capable blocks.
- [ ] Record three synchronized physical baselines at UART `460800`.

## Phase 1: reduce RAM and open 320x240

- [ ] Use the primary LCD buffer as two 8-row ping-pong DMA strips for HLV;
      do not allocate the optional 10,240-byte secondary buffer.
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

- [ ] In vertical and bilinear packed prediction, reuse the previous bottom
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
| Current Y7/U6/V6 baseline | pending | pending | 55.602 ms decode at 320x180 | 203,280-byte 320x240 core | baseline |
