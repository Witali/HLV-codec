# ESP32 decoder complexity-reduction TODO

This checklist covers decoder-only, bit-exact performance work for the compact
Y6/U5/V5 ESP32 path. The encoder and HLV bitstream syntax are not changed.

## Acceptance rules

Each experiment must:

1. preserve the complete compact-decoder reconstruction hash for
   `out/video.hlv`: `bdb0842a1e1a3a72`;
2. preserve the representative 120-frame QEMU hash:
   `be4876ff1c6b8461`;
3. improve Xtensa QEMU cycles per frame, or be removed and recorded as
   rejected;
4. avoid increasing decoder heap or static DRAM unless the trade-off is
   explicitly documented;
5. be committed separately after it passes the native simulator and QEMU.

QEMU is the available acceptance benchmark while the physical board is
offline. It does not model Flash-cache stalls, so IRAM and code-layout changes
must remain provisional until they are measured on the ESP32.

## Baseline

- Input: HLV v12, 320x180 at 15 fps, 8,947 frames, GOP 30.
- Padded decode grid: 320x192, 240 macroblocks per frame.
- Complete-film compact hash: `bdb0842a1e1a3a72`.
- Representative QEMU sample: four complete 30-frame GOP windows.
- QEMU hash: `be4876ff1c6b8461`.
- Native compact simulator: 381.49 us per frame over three complete passes.
- QEMU decoder cost: 611,435 guest cycles per frame.
- Fresh physical DIO-40 baseline: 3,764,549 cycles per frame on the
  representative sample.
- The final IRAM/QIO-80 physical maximum is 14,010,337 cycles, or 58.38 ms at
  240 MHz, below the 66.7 ms budget for 15 fps.

Average v12 syntax profile:

| Work item | Per frame |
| --- | ---: |
| Macroblocks | 240.0 |
| SKIP macroblocks | 151.54 |
| SPLIT_INTER macroblocks | 43.98 |
| INTER macroblocks | 10.47 |
| GLOBAL macroblocks | 11.34 |
| Coefficient symbols | 6,804.3 |
| Residual 4x4 blocks | 1,632.6 |
| Inverse WHT blocks | 1,082.6 |

Coefficient distribution:

- 61.9% of run symbols are zero;
- 59.4% of levels are `+1` or `-1`;
- one- and two-coefficient inverse-WHT cases are already specialised.

## Checklist

- [x] Establish a fresh simulator and QEMU baseline before modifying code.
- [x] Inline cached extraction, defer refill until data is required and keep
      packet-span/error handling in the slow helper.
- [x] Evaluate aligned 32-bit refill (rejected: 0.16% slower in QEMU).
- [x] Consume a complete in-cache Exp-Golomb code without nested bitreader
      calls.
- [x] Add a combined fast path for the frequent `run=0, level=+/-1`
      coefficient representation, with an exact fallback for every other
      value.
- [x] Write simple compact macroblocks directly:
  - FILL without residual (accepted);
  - PALETTE (evaluated and rejected: only 0.10% faster in QEMU);
  - zero-residual INTRA DC (evaluated and rejected: native v12 2.4% slower);
  - aligned LITERAL payloads through span copies instead of one call per byte
    (accepted for v13).
- [x] Evaluate stream-version specialisation (rejected: the narrow v12 mode
      parser is 3.1% slower natively; duplicating the complete loop would add
      substantially more code).
- [x] Fuse general inverse WHT, rounding, prediction addition and clipping to
      remove the temporary residual array and extra pass.
- [x] Evaluate selective IRAM placement (QEMU-neutral, but accepted after a
      14.74% improvement on the physical ESP32).
- [x] Re-run complete-film hashing, QEMU, ESP-IDF size reporting and document
      the cumulative result.
- [x] When the board becomes available, measure key/P frames separately and
      record P50/P95/max decode time. Do not sample at a period divisible by
      GOP 30.

## Already rejected approaches

Do not repeat these without a materially different implementation:

- global decoder `-O2` or `-Os`;
- fixed standalone Y6/U5/V5 unpack kernels;
- direct packed zero-residual INTER/GLOBAL copies;
- destructive residual-mask shifts;
- branchless WHT rounding;
- branchless compact quantisation.

Their measurements are retained in
`docs/ESP32_DECODER_OPTIMIZATION_TODO.md`.

## Results

| Variant | Native us/frame | QEMU cycles/frame | QEMU hash | Decision |
| --- | ---: | ---: | --- | --- |
| Fresh baseline | 381.49 | 611,435 | `be4876ff1c6b8461` | baseline |
| Inline cached read with lazy refill | 336.81 | 486,412 | `be4876ff1c6b8461` | accepted |
| Single-step cached Exp-Golomb | 337.51* | 454,488 | `be4876ff1c6b8461` | accepted |
| Aligned 32-bit empty-cache refill | 332.62* | 455,205 | `be4876ff1c6b8461` | rejected |
| Combined zero-run/unit-level VLC | 317.78 | 452,331 | `be4876ff1c6b8461` | accepted |
| Direct compact PALETTE output | 317.74 | 451,876 | `be4876ff1c6b8461` | rejected |
| Direct zero-residual FILL output | 316.89 | 450,885 | `be4876ff1c6b8461` | accepted |
| Fused inverse-WHT/add | 319.72 | 447,161 | `be4876ff1c6b8461` | accepted |
| Final clean validation | 318.79 | 447,161 | `be4876ff1c6b8461` | accepted |

The first bitreader step improves native throughput by 11.7% and reduces QEMU
guest cycles by 20.4%. Complete-film reconstruction remains
`bdb0842a1e1a3a72`. QEMU heap is unchanged. The application binary grows by
3,280 bytes because the short extraction path is now present at its call
sites; the partition still has 48% free.

The cached Exp-Golomb path removes a further 6.6% of QEMU cycles, for a
cumulative 25.7% reduction from baseline. It adds 800 bytes to the application
binary and does not change heap. The native number marked with `*` uses the
portable fallback because MSVC does not expose the GCC/Clang `clz` intrinsic
used by the Xtensa fast path; it is a hash check rather than a comparable
timing result.

An aligned 32-bit load plus byte swap is 0.16% slower than the existing four
byte refill iterations in QEMU. The source experiment was removed.

The combined coefficient VLC improves native throughput by 5.6% and removes a
further 0.47% of QEMU cycles. Its smaller QEMU effect shows that the preceding
cached Exp-Golomb optimisation already captures much of the same call
overhead. The cumulative QEMU reduction from the fresh baseline is 26.0%.

Direct compact PALETTE output saves only 455 QEMU cycles per frame (0.10%)
because v12 averages 1.09 palette macroblocks per frame. The much larger
mode-specific implementation is not justified by that result and was removed.

Direct output for zero-residual FILL macroblocks removes 0.32% of QEMU cycles
and 0.28% of native time. It reuses the existing packed frame and adds no heap
or static DRAM. Unlike the PALETTE trial, the compact fill helper is small and
is retained for difficult intra frames.

The LITERAL path is measured separately with
`BigBuckBunny_1080p_video-settings_v13_normalized.hlv`, because the v12
acceptance stream contains no literal macroblocks:

| v13 variant | Native us/frame | QEMU cycles/frame | QEMU hash |
| --- | ---: | ---: | --- |
| Byte-at-a-time LITERAL baseline | 341.17 | 472,383 | `6ca4210fb3a1edb7` |
| Packet-span LITERAL copy | 333.36 | 472,219 | `6ca4210fb3a1edb7` |

The complete v13 reconstruction remains `fe31eb325e6cb945`. Span copies improve
the complete native run by 2.3%, while the representative QEMU sample improves
by only 0.03% because it averages very few LITERAL blocks. The helper is
retained to bound literal-heavy keyframes and adds no packet or frame buffers.

A direct compact zero-residual INTRA DC path preserved both v12 and v13 hashes,
but slowed the native v12 test from 316.89 to 324.44 us/frame. It was removed
before QEMU; vertical and horizontal variants were not added because they
would enlarge the same already-regressed branch.

A v12-specialised mode parser removed the generic version tests but added a
dispatch and another copy of the prefix tree. Native v12 time regressed from
316.89 to 326.84 us/frame, so it was removed before QEMU. Duplicating the
complete decode loop would multiply the much larger prediction/residual switch
and is not supported by this result.

Fusing the general inverse WHT with prediction addition is 0.9% slower on the
x64 native filter but 0.8% faster in Xtensa QEMU. The target result is
accepted. It removes the 64-byte `raw` array inside the transform and the
32-byte caller-side `residual` array, reducing the deepest general residual
path by 96 bytes without changing heap or reconstructed frames.

Placing refill, slow bit extraction and Exp-Golomb helpers in IRAM moved about
4 KiB from Flash to IRAM. QEMU remained 447,161 cycles/frame (a 12-cycle total
difference over 120 frames, below the reported per-frame resolution), as
expected because it does not model the ESP32 Flash cache. On the physical
ESP32, three identical runs improved the mean from 3,764,549 to 3,209,621
cycles/frame (14.74%) while preserving `be4876ff1c6b8461`, so the placement is
now retained.

## Final off-board and physical result

The final clean validation produces:

- v12 complete-film hash `bdb0842a1e1a3a72`, 318.79 us/frame natively;
- v13 complete-film hash `fe31eb325e6cb945`, 328.21 us/frame natively;
- v12 QEMU hash `be4876ff1c6b8461`, 447,161 cycles/frame;
- QEMU heap 306,256 bytes free, largest block 172,032 bytes, unchanged;
- normal player binary `0x416f0` bytes, with 83% of the application partition
  free;
- Flash Code 171,328 bytes;
- static DRAM 54,788 bytes;
- IRAM 55,783 bytes, with 75,289 bytes remaining.

The physical decoder-only result with the retained IRAM helpers and QIO flash
at 80 MHz is 2,776,047 cycles/frame (11.57 ms), P50 1,995,995, P95 8,673,122
and maximum 14,010,337 cycles (58.38 ms). Five keyframes average 8,350,998
cycles and 115 P-frames average 2,533,658 cycles. All three runs produce
`be4876ff1c6b8461`.

Relative to the fresh v12 baseline, native time is down 16.4% and QEMU decoder
cycles are down 26.9%. At the nominal 240 MHz target the QEMU instruction-count
normalisation rises from 392.5 to 536.7 decoder-only fps. These are
instruction-count comparisons, not physical playback rates.

No accepted decoder change adds heap or static frame storage. Physical
Flash-cache effects and key/P-frame distributions have now been validated.

## 320x240 v13 streaming optimisation pass

This pass targets `out/VID_20260522_181611.hlv`, the current difficult
HLV v13 320x240 at 30 fps workload. It keeps the HLV container and bitstream
standard unchanged. The preceding v12 320x180 results remain useful regression
tests, but they are not representative of this file's much denser residual
stream.

### Acceptance rules

Each experiment must:

1. preserve the complete 3,359-frame compact reconstruction hash
   `3fecc5b367d31b8c`;
2. keep packet CRC verification enabled and accept the same standard HLV files;
3. pass both the bounded two-window streaming decoder and the existing
   segmented packet decoder;
4. measure full-film native time and physical ESP32 per-frame time;
5. retain a change only when the physical-board result improves beyond normal
   run-to-run variation without an unacceptable RAM or code-size increase;
6. record rejected experiments here and remove their source changes.

### Baseline

- Input size: 82,474,306 bytes, or about 24.5 KiB per frame including audio.
- Compact frame storage plus working rows: 174,480 bytes.
- Streaming packet window: two 7,680-byte buffers.
- Native x64 O3/BR32: 1,596.69 us/frame over one complete timed pass.
- Physical ESP32, first 300 frames:
  - SD read: 8,503.6 us average;
  - decode excluding `fread`: 97,863.6 us average;
  - render: 44,482.2 us average;
  - observed presentation rate: 9.362 fps;
  - 46 skipped presentations and no audio underrun.

Average syntax work per frame:

| Work item | Per frame |
| --- | ---: |
| Macroblocks | 300.0 |
| SKIP | 43.86 |
| INTER | 47.40 |
| GLOBAL | 17.81 |
| SPLIT_INTER | 156.29 |
| FILL | 6.71 |
| PALETTE | 4.11 |
| LITERAL | 0.03 |
| Intra modes, combined | 23.79 |
| Coefficient symbols | 26,592.7 |
| Residual 4x4 blocks | 4,968.3 |
| Zero residual blocks | 1,162.4 |
| DC-only blocks | 450.4 |
| Inverse WHT blocks | 3,356.2 |

### Ordered checklist

- [x] Add low-overhead, compile-time stage profiling for:
  - streaming CRC;
  - entropy/VLC and residual parsing;
  - motion/intra prediction;
  - inverse WHT and residual addition;
  - compact Y6/U5/V5 packing.
- [x] Compare the current table CRC with the ESP32 ROM CRC32 implementation,
      an IRAM-resident loop and a bounded-memory word-at-a-time implementation.
- [x] Cache compact correction division and tile lookup once per 8-pixel
      span instead of once per reconstructed sample.
- [ ] Fuse compact motion prediction and output:
  - generate prediction in four-pixel groups;
  - apply residuals in a 4x4 scratch block;
  - immediately write packed Y6/U5/V5;
  - accumulate the existing local correction values in the same pass.
- [ ] Re-evaluate direct packed no-residual motion for this denser v13 file
      only with a materially different fused implementation. Do not restore
      the previously rejected standalone packed-copy experiment.
- [ ] Profile the coefficient-count histogram beyond two coefficients, then
      test only frequent specialised WHT cases.
- [ ] Evaluate a hand-unrolled Xtensa general inverse WHT and selective IRAM
      placement without increasing the decoder's frame heap.
- [ ] Cache each unpacked HLV chroma row across its two luma rows and fuse
      compact luma unpacking with RGB565 conversion.
- [ ] Add an optional encoder RDO decode-cost term. Compare equal-quality
      outputs by PSNR/SSIM, bitrate, packet peak, ESP32 decode time and render
      time. The output must remain a standard HLV stream.
- [ ] Consider a bounded second-core transform/prediction job queue only after
      the single-core decoder work above. CPU0 already renders the preceding
      frame concurrently, so accept this only if synchronisation and queue
      memory produce a clear end-to-end improvement.

### Results

| Variant | Native us/frame | ESP32 decode us/frame | Hash | Decision |
| --- | ---: | ---: | --- | --- |
| Streaming baseline | 1,596.69 | 97,863.6 | `3fecc5b367d31b8c` | baseline |
| Stage profiler, 30 frames | n/a | 105,057.9 | unchanged decoder | accepted as an opt-in diagnostic |
| Four-byte unrolled CRC | hash pass | 103,557.7 | `3fecc5b367d31b8c` | accepted |
| Per-tile compact correction | 1,403.35 | 85,797.1 | `3fecc5b367d31b8c` | accepted |

The opt-in stage build uses `HLV1_STAGE_PROFILE=ON`; release builds leave every
timer read compiled out. On the first 30 physical frames it reports:

| Profiled stage | Average us/frame | P95 us/frame |
| --- | ---: | ---: |
| Streaming CRC | 1,439.6 | 2,842 |
| Prediction | 59,422.8 | 77,968 |
| Residual parsing/addition | 37,164.6 | 53,364 |
| Inverse WHT, included in residual | 9,757.0 | 11,503 |
| Compact packing | 9,373.5 | 10,713 |

The profiler itself raises the externally measured decoder average by about
7.4%, chiefly because it reads the cycle counter inside thousands of residual
blocks. It is therefore a stage-share diagnostic, not a release-performance
baseline. The profile makes prediction the first structural target. CRC is
only about 1.4 ms/frame, so even eliminating it completely cannot close the
30 fps gap.

CRC variants were compared over the same first-frame window:

| CRC implementation | CRC avg us/frame | Decision |
| --- | ---: | --- |
| One-byte table loop | 1,439.6 | baseline |
| Same loop in IRAM | 1,447.2 | rejected, no improvement |
| ESP32 ROM `crc32_le` | 1,451.4 | rejected, no improvement |
| Four-byte unrolled loop, same 1 KiB table | 846.6 | accepted, -41.2% |

The retained loop adds no lookup tables or heap and preserves the complete
streaming reconstruction hash. It saves about 0.6 ms/frame, so it is useful
but deliberately not treated as a solution to the decoder bottleneck.

The compact correction originally repeated a signed division, correction
table lookup and tile-address calculation for every prediction or display
sample. Processing each span up to the next 8x8 tile boundary reuses the
quotient and remainder while preserving the exact 4x4 threshold phase.
The complete 3,359-frame hash is unchanged. On the first 31 physical frames:

| Metric | Four-byte CRC baseline | Per-tile correction | Change |
| --- | ---: | ---: | ---: |
| Decode average | 103,557.7 us | 85,797.1 us | -17.2% |
| Prediction average | 57,973.7 us | 43,394.2 us | -25.1% |
| Render average | 45,140.8 us | 37,228.5 us | -17.5% |
| Observed presentation | 8.496 fps | 10.211 fps | +20.2% |

The syntax profile also shows only 0.02 zero-residual macroblocks per frame.
The existing SKIP representation is still valuable at 43.86 macroblocks per
frame, but a new decode-oriented stream version does not need to spend a
branch on optional residuals for the dense INTER/GLOBAL/SPLIT paths used by
this source.
