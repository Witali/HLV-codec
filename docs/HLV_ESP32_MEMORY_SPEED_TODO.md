# HLV ESP32 memory and unpack performance TODO

## HLV v15 acceptance (2026-07-31)

- [x] Portable v14/v15 round-trip and malformed-stream tests pass.
- [x] Compact, expanded, 257-byte refill and single-reference segmented
      simulator paths produce the same 60-frame reconstruction hash.
- [x] Xtensa QEMU decodes 30 v15 frames with final hash
      `cff1e112a17aba41` at 2,252,229 cycles/frame average.
- [x] Physical ESP32-2432S028 decodes the complete 60-frame QVGA/30 v15 test
      without frame gaps or display skips. Decode averages 61.812 ms and total
      work 98.004 ms, so this high-quality QVGA profile remains below real
      time despite the smaller bitstream.
- [x] Test asset removed from SD after CRC verification and playback; original
      `play.txt`, user/demo media and persistent `crc32.txt` preserved.

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

Full-file motion-vector analysis shows that previous-frame prediction is used
by 94.42% of `320x180` macroblocks and 93.94% of `320x240` macroblocks. Both
files actually use the complete declared vertical range from -8.0 through
+8.0 rows. Reducing the existing square search radius would therefore remove
real candidates as well as reducing horizontal search. A separate
encoder-side vertical limit is not justified for the current assets: the
32-luma-row rolling store already covers a 16-row macroblock plus the declared
eight-row reach, while larger stores are only inter-core scheduling slack.

## Phase 0: reproducible measurements

- [x] Record current native compact hashes and timing for both picture sizes.
- [x] Record current Xtensa QEMU cycles, size and heap for representative GOP
      windows from both files.
- [x] Port the opt-in stage profiler from commit `3f541bf` to the current C99
      Y7/U6/V6 decoder. Keep every timer read compiled out by default.
- [x] Add allocation-stage heap logging for `ESP_PLATFORM`, including the
      largest 8-bit and DMA-capable blocks.
- [x] Record three synchronized physical baselines at UART `460800`.

## Phase 1: reduce RAM and open 320x240

- [x] Select the reference strategy by picture size and measured heap:
  - prefer dual references and an O(1) pointer swap at resolutions where both
    packed frames fit without reducing the player safety reserve;
  - retain one reference plus 32 rolling luma rows for `320x240`;
  - compare complete physical work, not decoder time alone.
  The retained threshold uses dual references through the padded `320x192`
  area and single-reference storage above it. If a smaller picture cannot
  allocate both references, decoder creation retries the single-reference
  path.
- [x] Add a one-reference HLV fast path modelled on BPV v7:
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
- [x] Measure the one-reference target at `320x240`: one 97,800-byte packed
      Y7/U6/V6+Q4 reference, 13,040 bytes of rolling rows and 7,680 unpacked
      working-row bytes, for 118,520 bytes instead of 203,280 bytes.
- [ ] Re-evaluate use of only the primary LCD buffer after the one-reference
      path fits. The isolated attempt saved 10,240 bytes but made 320x180
      render average 1.78% slower and still did not open 320x240.
- [x] Evaluate replacing HLV's 16 KiB stdio read-ahead plus decoder refill
      allocation with one fixed-capacity asynchronous path. An 8 KiB
      double-bank with 4 KiB direct VFS reads was rejected: CPU0 reader
      priority 2 displaced rendering, while priority 1 starved decoding.
- [x] Verify in that experiment that only the reader owns the video descriptor
      and compressed spans pass directly from each bank to the decoder. The
      complete implementation was removed after the physical regression.
- [x] Test a valid packet larger than the ring and compare its decoded hash
      with contiguous and direct-file decode.
- [ ] Reorder large allocations only if heap logs show fragmentation rather
      than insufficient total capacity.
- [ ] Reduce the full-width 7,680-byte unpacked working rows to bounded
      macroblock/4x4 storage as part of fused reconstruction.
- [ ] Record stack high-water marks. HLV `320x240` now opens with audio enabled;
      after decoder allocation the board reports 89,048 free heap bytes and
      an 81,920-byte largest block.

## Phase 1A: remove one-reference pipeline stalls

- [x] Measure row-guard wait time separately from prediction, residual,
      packing and input time.
- [x] Test a render-ahead watermark:
  - render at least the first 16 source rows before CPU1 starts decoding the
    next frame;
  - continue rendering the remaining rows concurrently;
  - keep the row guard as a correctness backstop for later CPU0 stalls;
  - reject the scheduling change if complete physical work or observed rate
    improves by less than 0.5%.
  It removed guard stalls and reduced decoder time, but serialized the
  `beginPresentation()` audio-clock wait ahead of CPU1 decode and reduced
  actual throughput. The source change was removed.
- [x] Re-evaluate replacing the yielding row-guard spin with a task
      notification. With the watermark the guard averaged only 10 us, and
      without it the median remains 10 us; notification complexity is not
      justified for rare A/V scheduling stalls.

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

- [x] Add an unrolled byte-aligned eight-sample Y7 pack path. U6 already uses
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

- [x] Cache each corrected U6/V6 row across its two Y7 luma rows.
- [ ] Fuse aligned Y7 unpack, Q4 correction and RGB565 conversion without an
      intermediate full luma row.
- [ ] Precompute corrected U/V colour contributions once per chroma sample and
      reuse them for both associated luma rows.
- [ ] Convert and submit RGB565 strips through both LCD DMA buffers so CPU0
      conversion overlaps the previous transfer wherever the display driver
      permits it.
- [ ] Measure conversion CPU cycles separately from LCD DMA waits.

## Phase 5: encoder-side decode cost

- [ ] Compare `--decode-cycle-weight` values `0`, `0.01`, `0.025` and `0.05`
      at matched PSNR/SSIM.
- [ ] Record bitrate, maximum packet size, prediction mix, coefficient/WHT
      counts, physical decode time and render time.
- [ ] Add a separate ESP32-oriented encoding profile only if it preserves the
      requested quality while producing a clear end-to-end improvement.
- [ ] Do not reduce the current eight-pixel motion radius without matched
      quality/bitrate tests: both Danila assets use actual vectors at -8 and
      +8 rows.

## Current priority order

1. fixed-denominator packed prediction kernels;
2. fused prediction/residual/packing;
3. fused display unpack/Q4/RGB565;
4. inverse-WHT and encoder decode-cost experiments.

At `320x240`, 30 fps requires both decode and render wall time below 33.3 ms.
The current approximately 68.4 ms decode and 34.1 ms render measurements mean
that isolated micro-optimisations cannot reach that target; retained work must
reduce a measured dominant stage or remove pipeline serialization.

The opt-in C99 profile on the 120-frame 320x240 49 dB asset reports:

| Stage | Average |
| --- | ---: |
| Complete decode | 68.291 ms |
| Compressed input | 35.686 ms |
| CRC | 3.276 ms |
| Prediction | 8.502 ms |
| Residual, including WHT | 12.374 ms |
| Inverse WHT alone | 1.569 ms |
| Y7/U6/V6 packing | 2.376 ms |
| Reference commit, including guard | 4.443 ms |
| Row-guard wait alone | 4.004 ms |

Each packet averages 91,310 compressed bytes and 12.41 refills through the
fixed 7,680-byte decoder buffer. The median row-guard wait is only 10 us;
occasional audio-clock/render scheduling stalls raise its average. Input is
the first structural target. The measured stages overlap (inverse WHT is part
of residual and row guard is part of commit), so they must not be summed.

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
- 8 KiB asynchronous direct-VFS HLV double bank on CPU0 at reader priority 1
  or 2.
- 16-row HLV render-ahead before `beginPresentation()` completes.

## Results

| Experiment | Native | QEMU | Physical ESP32 | RAM | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Current Y7/U6/V6 baseline | pending | 2,959,678 cycles at 320x180; 3,696,921 at 320x240 | 55.602 ms decode at 320x180 | 203,280-byte 320x240 core | baseline |
| Stronger QEMU frame hash and large-packet coverage | pending | hashes `150eef90ad52b1ae` at 320x180 and `612a072f0b034761` at 320x240; packets 46,652 and 58,551 bytes through a 7,680-byte refill | not applicable | no player change | retained |
| Primary-only LCD DMA storage | not applicable | not applicable | render 30.869 -> 31.419 ms (+1.78%); 320x240 still failed | -10,240 bytes | rejected in isolation |
| Allocate both compact luma planes before chroma | unchanged hash | unchanged hash | second 320x240 frame allocation still failed with a 13,312-byte largest block for a 14,400-byte V plane | unchanged | rejected |
| Reuse packed vertical/bilinear source row | pending | 2,622,911 cycles at 320x180 (-11.38%); 3,255,409 at 320x240 (-11.94%); hashes unchanged | three-run median 54.766 ms decode (-1.50%), about 0.88% lower complete work | unchanged | retained |
| Reuse corrected chroma row during rendering | unchanged | not applicable | three-run median render 26.800 ms (-13.18%); complete work 81.642 ms (-5.58%); no audio errors | unchanged | retained |
| One reference + 32 rolling luma rows | full hashes `7ff021b48acfe095` at 320x180 and `005878155c7f3057` at 320x240; dual, expanded, segmented and 257-byte refill paths agree over 3,358 frames | 3,280,612 cycles at 320x240 (-11.26% from baseline, +0.77% from dual-reference row-reuse); hash `612a072f0b034761` unchanged | 320x240 opens; 120 frames, no gaps/audio errors, 69.052 ms decode and 103.448 ms complete work | 118,520-byte 320x240 core, -84,760 bytes; board heap 89,048, largest 81,920 | retained; required to open 320x240 |
| 64 instead of 32 rolling luma rows | unchanged | 3,280,647 cycles at 320x240, effectively unchanged | 320x180 work 86.461 -> 86.122 ms (-0.39%); periodic waits remained | +13,040 bytes | rejected |
| Audio reader priority 3 -> 1 | not applicable | not applicable | no underrun, but decode 59.661 -> 60.940 ms and observed rate 16.801 -> 16.468 fps | unchanged | rejected |
| Byte-aligned eight-sample Y7 pack | full 3,358-frame hash `005878155c7f3057` unchanged; all four decoder/input paths agree | 3,224,408 cycles at 320x240 (-1.71%); hash `612a072f0b034761` unchanged | three-run median decode 68.447 ms (-0.88%) and complete work 102.534 ms (-0.88%); no gaps/audio errors | unchanged | retained |
| Opt-in current-C99 stage profiler | release build unchanged; every timer read compiled out with default `HLV1_STAGE_PROFILE=OFF` | 30-frame 320x240 hash `93531122144bec97`; 58,551-byte packet through 7,680-byte refill | 120 frames, no gaps/audio errors; input 35.686 of 68.291 ms decode; row guard 4.004 ms average, 10 us median | release unchanged | retained diagnostic |
| Adaptive dual references through padded 320x192 | full 3,358-frame dual/single hashes agree | identical 30-frame 320x180 hash `a59ea6feba53a6dc`; 2,385,648 dual vs 2,404,625 single cycles (-0.79%); 320x240 remains single with hash `93531122144bec97` | three-run median at 320x180: decode 60.396 -> 54.968 ms (-8.99%), work 86.732 -> 81.728 ms (-5.77%), observed 16.664 -> 17.508 fps, skips 22 -> 20; final release test decoded 300 consecutive frames with no gaps/audio errors at 54.622 ms decode and 81.429 ms work | +65,200 bytes at padded 320x192; 320x240 remains 118,520 bytes; +64-byte app image, IRAM unchanged | retained |
| Fixed 8 KiB async zero-copy double bank, 4 KiB direct VFS reads | experimental 113-byte span path preserved full 3,358-frame hash `7ff021b48acfe095` | not applicable; QEMU does not model SD/task scheduling | priority 2 preliminary run: input 14.568 ms and decode 41.346 ms, but render 40.516 ms and work 81.862 ms (+0.16%), skips 22; priority 1 three-run median: decode 59.130 ms (+7.57%), work 86.073 ms (+5.32%), observed 15.967 fps, skips 24; no gaps/audio errors | compressed buffers -15,872 bytes, but reader stack and queues reduce net saving to about 11 KiB | rejected; source removed |
| Render first 16 rows before starting next single-reference decode | unchanged full decoder hashes | not applicable; player scheduling is not exercised | strict three-run 320x240 A/B: decode 68.745 -> 64.448 ms (-6.25%), work 102.871 -> 98.565 ms (-4.19%), row guard 4.246 ms -> 10 us and skips 23 -> 22, but observed rate 14.673 -> 14.000 fps (-4.59%) because the audio-clock wait moved ahead of decode | unchanged | rejected; source removed |
| 32 KiB instead of 16 KiB stdio read-ahead | not applicable | not applicable | two-run average decode 67.724 ms, about -0.9%; input only -0.5% | +16,384 bytes | rejected; wrong speed/RAM trade-off |
| Remove stdio read-ahead, retain `_IONBF` `fread` | not applicable | not applicable | input 732.399 ms, decode 761.522 ms | -16,384 bytes | rejected; severe regression |
| Remove stdio read-ahead, direct ESP VFS `read` | not applicable | not applicable | input 42.311 ms, decode 74.348 ms (+8.8%) | -16,384 bytes | rejected; speed regression |
