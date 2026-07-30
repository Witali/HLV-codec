# MPEG-4 Simple Profile ESP32 decoder optimization TODO

This checklist tracks performance and memory experiments for AVI/MPEG-4
Simple Profile playback on the original ESP32-2432S028 board. The C99
firmware is the primary physical target; equivalent behavior must remain
available in the preserved C++ firmware.

## Acceptance method

- Measure decoder candidates in QEMU first and require an unchanged decoded
  frame hash.
- Measure promising candidates on the physical ESP32 at 240 MHz with the same
  AVI file and flash configuration.
- Run at least three reset-to-reset short trials and compare median decode,
  render and complete-work time. Treat changes below 0.5% as neutral unless
  repeated measurements are stable.
- Keep a decoder optimization only when physical decode time improves,
  decoded output remains identical and internal-memory headroom remains safe.
- Complete a full-file acceptance run after each retained group of changes.
  Require every video packet to decode, no frame-sequence gaps, and zero audio
  rebuffers, missing samples or inserted silence.
- Record and remove only the files uploaded specifically for a physical test.
  Preserve pre-existing SD files, `play.txt` and `crc32.txt`.
- Commit each retained optimization separately. Revert rejected candidates
  but record their measurements and reason here.

## Starting baseline

The retained compact decoder stores the previous and current pictures as two
independently allocated Y6/U5/V5 frames with signed Q4 block-average
corrections. It reconstructs one 16-luma-row macroblock row in byte-planar
form and then packs that row. Compressed packets are consumed through a
reusable 4 KiB refill buffer; only bounded VOL configuration data is retained
contiguously.

Current deterministic QEMU result:

- `2,989,588` cycles/frame;
- decoded hash `1d04abfdf28c73cf`;
- decoder-owned memory `193,880` bytes.

Current physical full-file result:

- `3,357 / 3,357` video packets decoded;
- observed rate `12.236` fps from a 30 fps source;
- average decode `75,425.3 us`;
- average render `28,628.7 us`;
- average complete work `104,054.0 us`;
- 456 deliberately omitted displays;
- zero sequence gaps, audio underruns, rebuffers and silence insertion.

## Priority 0: measure the physical hot paths

- [x] Add a compile-time-gated MPEG-4 stage profile with negligible cost when
      disabled.
- [x] Measure bitstream/VLC/dequantization, motion-vector decoding and motion
      compensation, inverse DCT plus prediction add, compact-row packing and
      decoder control separately.
- [x] Count I/P pictures, skipped macroblocks, CBP classes, one/four-vector
      macroblocks, half-pel directions, edge-clamped predictions and sparse
      coefficient patterns.
- [x] Print aggregate counters outside the measured decode interval.
- [x] Capture three physical baseline runs and use them to choose the next
      implementation target.

The deterministic 60-picture profiling corpus decodes to
`b826825f344bc2e3`. All three physical runs produced the same cycle counts and
class counters. The compile-time-disabled QEMU build averaged 3,037,526
cycles/picture. The profiled QEMU build averaged 3,043,299 cycles/picture,
including instrumentation, and retained the same hash.

| Physical stage | Cycles/picture | Share of total |
| --- | ---: | ---: |
| Motion compensation | 8,959,529 | 51.65% |
| Compact-row packing | 3,302,835 | 19.04% |
| IDCT plus prediction add | 2,344,100 | 13.51% |
| VLC/dequantization | 2,081,279 | 12.00% |
| Motion-vector decoding | 263,466 | 1.52% |
| Compressed-input reads | 116,874 | 0.67% |
| Complete decode | 17,348,424 | 100% |

The corpus contains 16,807 inter macroblocks and every one uses a single
motion vector; no four-vector macroblocks occur. Compact prediction performs
28,340 integer, 20,518 horizontal, 20,798 vertical and 29,308 diagonal calls.
Only 6,395 predictions touch a picture edge. The next implementation target
is therefore direct interior half-pel prediction from compact reference rows,
followed by compact-row packing.

## Priority 1: specialize sparse inter IDCT

- [x] Measure a direct DC-only inter residual plus prediction kernel.
- [ ] Measure and, when common, add exact one-row, one-column and two-column
      sparse kernels.
- [ ] Avoid clearing untouched coefficient slots when a sparse representation
      is faster.
- [ ] Fuse inverse transform, prediction addition, clipping and rolling-row
      stores where that preserves exact pixels.
- [ ] Retain each specialization independently only after QEMU hash and
      physical A/B acceptance.

A direct DC-only kernel bypassed the existing `idctcol1` plus `idctrow1`
dispatch and produced the unchanged `b826825f344bc2e3` hash. It improved the
QEMU benchmark from 2,750,352 to 2,749,294 cycles/picture, only 0.038%.
This is below the 0.5% acceptance threshold, so the candidate was removed
without flashing it to the physical board.

## Priority 2: decode compact motion compensation directly

- [x] Measure integer, horizontal, vertical and diagonal half-pel frequency
      and patch-construction cost.
- [x] Add direct Y6/U5/V5 horizontal and vertical half-pel kernels without a
      9x9 or 17x17 temporary patch.
- [x] Add a direct diagonal kernel only if its interpolation and compact
      unpack cost improve physically.
- [x] Specialize common safe 16x16 one-vector/CBP classes while preserving the
      four-block `pred_block` layout expected by residual reconstruction.
- [x] Keep edge-clamped and uncommon cases on the verified generic path.

Interior half-pel prediction now unpacks only one or two rolling compact
sample rows (at most 17 bytes each) instead of constructing a complete 9x9 or
17x17 byte-planar patch. Integer predictions keep the direct-copy path and
edge-clamped predictions keep the verified generic patch path.

The compile-time-disabled QEMU benchmark improved from 3,037,526 to
3,000,045 cycles/picture (1.23%) with the unchanged
`b826825f344bc2e3` hash. In three identical profiled physical trials, motion
compensation improved from 8,959,529 to 8,269,539 cycles/picture (7.70%) and
complete decode improved from 17,348,424 to 16,659,074 cycles/picture
(3.97%).

The retained production build completed the 3,357-picture physical file at
12.532 fps, versus 12.236 fps before this change. Average decode time improved
from 75,425.3 to 73,179.8 us (2.98%); average complete work improved from
104,054.0 to 101,793.2 us (2.17%). All 3,357 sequence numbers were
consecutive and audio reported zero underruns, rebuffers and inserted silence.

## Priority 3: remove redundant rolling-row traffic

- [x] Specialize byte-aligned eight-sample Y6/U5/V5 packing when no
      reconstructed byte output is requested.
- [ ] Write skipped and all-zero-residual predictions directly to the compact
      output when reference lifetime and render guards make this safe.
- [ ] Pack coded 8x8 blocks while reconstructed pixels are still cache-hot.
- [ ] Compute Q4 block-average corrections during the direct compact write.
- [ ] Preserve exact compact-frame checksums and row-level renderer safety.

The retained aligned packer derives the signed correction residual from the
sum of eight source samples minus the sum of their quantized codes. The
generic path remains active for partial blocks, other bit depths and callers
that request reconstructed bytes.

After the direct half-pel optimization, the compile-time-disabled QEMU
benchmark improved from 3,000,045 to 2,750,352 cycles/picture (8.32%) with
the unchanged `b826825f344bc2e3` hash. C++ QEMU produced the same hash at
2,750,301 cycles/picture.

All three profiled physical trials were identical. Compact-row packing
improved from 3,302,835 to 2,230,095 cycles/picture (32.48%) and complete
decode improved from 16,659,074 to 15,528,990 cycles/picture (6.78%).
Decoder-owned memory remained 194,056 bytes.

The full 3,357-picture production acceptance run improved from 12.532 to
13.341 fps. Average decode time improved from 73,179.8 to 67,386.3 us
(7.92%), and complete work improved from 101,793.2 to 95,920.9 us (5.77%).
Every sequence number was consecutive and audio again reported zero
underruns, rebuffers and inserted silence.

## Priority 4: overlap independent work

- [ ] Verify current core affinity, queue waits and reference/display lifetime.
- [ ] Pipeline rendering of picture N with decoding of picture N+1 when the
      output/reference frame cannot be overwritten early.
- [ ] Preserve the two compact predictive pictures; a single total picture
      buffer is unsafe for unrestricted MPEG-4 P-frame motion vectors.
- [ ] Measure wall-clock throughput, not only per-task CPU time.

## Priority 5: flash and code placement

- [ ] Capture exact physical A/B results for DIO 80 MHz and QIO 80 MHz on the
      installed flash before changing the default.
- [ ] Measure motion compensation IRAM placement independently.
- [ ] Place only stage-profiled hot code in IRAM and keep byte-addressed
      working data in DRAM.
- [ ] Track IRAM use, free heap and largest free block for every retained
      placement.

## Priority 6: speed-oriented encoding profile

- [ ] Add encoder analysis that reports skip/CBP-zero rate, residual sparsity,
      motion-vector precision and I/P picture cost.
- [ ] Evaluate a separately named ESP32-speed preset that favors sparse
      residuals and integer motion vectors while remaining MPEG-4 Simple
      Profile.
- [ ] Preserve the source frame rate and audio normalization.
- [ ] Record output size, decoded PSNR/quality and physical decode speed.
- [ ] Do not replace the normal quality profile unless the trade-off is
      explicit and accepted.

## Explicit non-targets

- Do not feed individual MPEG-4 DCT blocks to `esp_new_jpeg`. Its supported
  interface consumes complete JPEG entropy-coded images and does not expose a
  raw 8x8 inverse-DCT primitive.
- Do not silently reduce source frame rate to meet the device budget.
- Do not merge a QEMU-only speedup that is neutral or slower on the physical
  ESP32.
- Do not grow the reusable compressed-input refill buffer to the largest AVI
  packet.

## Test infrastructure follow-up

- [ ] Remove matching cached records from `crc32.txt` atomically when
      `HLVDELETE` removes a test file, and avoid duplicate records on upload.
- [ ] Make the uploader completion parser accept an exact valid response
      before unrelated bytes emitted during the UART baud transition.
