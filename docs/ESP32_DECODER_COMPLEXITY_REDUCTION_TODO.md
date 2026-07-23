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
- Last physical reference: about 5,488,760 cycles per frame on the
  representative sample.
- Observed difficult keyframes: 96-99 ms at 240 MHz, versus the 66.7 ms budget
  for 15 fps.

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
- [ ] Optimise the bitreader fast path:
  - inline the common cached extraction;
  - defer refill until data is actually required;
  - keep packet-block transitions and errors in a cold slow path;
  - load multiple input bytes where safe;
  - consume a complete in-cache Exp-Golomb code without nested bitreader calls.
- [ ] Add a combined fast path for the frequent `run=0, level=+/-1`
      coefficient representation, with an exact fallback for every other
      value.
- [ ] Write simple compact macroblocks directly:
  - FILL without residual;
  - PALETTE;
  - zero-residual INTRA DC/vertical/horizontal where profitable;
  - aligned LITERAL payloads through span copies instead of one call per byte.
- [ ] Evaluate stream-version-specialised v12/v13 decode loops while retaining
      the portable generic fallback.
- [ ] Fuse general inverse WHT, rounding, prediction addition and clipping to
      remove the temporary residual array and extra pass.
- [ ] Evaluate selective IRAM placement for only the final small hot paths.
- [ ] Re-run complete-film hashing, QEMU, ESP-IDF size reporting and document
      the cumulative result.
- [ ] When the board becomes available, measure key/P frames separately and
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
