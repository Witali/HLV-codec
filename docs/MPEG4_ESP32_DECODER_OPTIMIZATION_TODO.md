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

For the current optimization series, keep the uploaded
`Danila_320x240_30fps_MPEG4SP_35dB.avi` on the SD card so every retained
candidate uses the exact same bytes. Delete this one test asset only after
all checklist work is complete, then restore `play.txt` and `crc32.txt` and
verify their final sizes with a fresh directory listing.

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
- [x] Measure exact one-row, one-column and two-column coefficient patterns.
- [ ] Add exact one-row, one-column and two-column sparse kernels independently.
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

Of the profiled inter residuals, 12,665 blocks occupy one coefficient row,
11,235 occupy one coefficient column and 7,203 occupy exactly two columns.
The shape counters use actual coefficient bitmaps and overlap where
applicable. A narrow candidate reused `idctrow1` after the existing column
transform when an early-zigzag block occupied only the DC column. It retained
the decoded hash but improved QEMU from 2,701,213 to 2,698,905
cycles/picture, only 0.085%. The dispatch-only candidate was removed without
physical flashing; a useful specialization must instead fuse more of the
transform, prediction add and output stores.

## Priority 2: decode compact motion compensation directly

- [x] Measure integer, horizontal, vertical and diagonal half-pel frequency
      and patch-construction cost.
- [x] Add direct Y6/U5/V5 horizontal and vertical half-pel kernels without a
      9x9 or 17x17 temporary patch.
- [x] Add a direct diagonal kernel only if its interpolation and compact
      unpack cost improve physically.
- [x] Specialize common safe 16x16 one-vector/CBP classes while preserving the
      four-block `pred_block` layout expected by residual reconstruction.
- [x] Split signed Q4 compact-reference corrections without signed division.
- [x] Specialize clipping for the bounded -8..14 Q4 correction domain emitted
      by the Y6/U5/V5 packer.
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

Biasing the complete signed Q4 correction domain by 128 permits an exact
unsigned quotient/remainder split, avoiding signed floor division in every
corrected compact-reference span. Native correction tests pass. QEMU improved
from 2,750,352 to 2,701,213 cycles/picture (1.79%); C++ produced the same hash
at 2,701,200 cycles/picture.

All three physical profile trials were identical. Motion compensation
improved from 8,268,518 to 8,112,942 cycles/picture (1.88%), and complete
decode improved from 15,528,990 to 15,400,164 cycles/picture (0.83%).
Decoder memory and heap headroom were unchanged. The production build
completed all 3,357 pictures at 13.620 fps, versus 13.341 fps before this
change. Average decode time improved from 67,386.3 to 65,611.0 us (2.63%);
complete work improved from 95,920.9 to 93,838.3 us (2.17%). Sequence gaps
and every audio recovery counter remained zero.

Packed corrections are limited to -8..14 Q4. In that domain, an expanded
sample changes by only -1, 0 or 1 and the quantized maximum always has room
for +1, so the common path needs only an underflow clamp. The generic
`int8_t` correction path retains both clamps for external callers. Native
tests, C QEMU and C++ QEMU retained the same decoded output. C QEMU improved
from 2,701,213 to 2,656,549 cycles/picture (1.65%); C++ measured 2,656,554.

Three physical profile trials were bit-for-bit identical. Motion
compensation improved from 8,112,942 to 7,875,030 cycles/picture (2.93%) and
complete decode from 15,400,164 to 15,137,528 cycles/picture (1.70%), with
unchanged decoder memory and heap headroom. A full-file run decoded all
3,357 pictures with no sequence gaps, rebuffers, underruns or inserted
silence. Its wall-clock rate was 13.475 fps and average decode time was
66,462.1 us, versus 13.620 fps and 65,611.0 us in the preceding run; unlike
the deterministic decoder profile, this noisy end-to-end run did not improve.

A four-phase unroll of the same correction loop preserved output but slowed
QEMU from 2,656,549 to 2,676,262 cycles/picture (0.74%). It was removed
without physical flashing.

## Priority 3: remove redundant rolling-row traffic

- [x] Specialize byte-aligned eight-sample Y6/U5/V5 packing when no
      reconstructed byte output is requested.
- [x] Copy skipped macroblocks directly between compact pictures when
      reference lifetime and render guards make this safe.
- [ ] Write all-zero-residual predictions directly to the compact output.
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

Skipped macroblocks have zero motion and no residual, so their six compact
8x8 blocks and Q4 corrections are copied directly from the previous compact
picture instead of rereading and repacking the rolling byte rows. Non-skipped
blocks retain the verified aligned packer.

C QEMU improved from 2,656,549 to 2,629,072 cycles/picture (1.03%) and C++
measured 2,629,036, both with the unchanged `b826825f344bc2e3` hash. The
profile corpus contains 40 skipped macroblocks.

Two of three physical profile trials measured exactly 14,956,841
cycles/picture; the third measured 14,956,957. The median complete decode
improved from 15,137,528 cycles/picture by 1.19%, while compact packing
improved from 2,214,495 to 2,176,201 cycles/picture (1.73%). Decoder memory,
free heap and largest free block remained unchanged.

The production acceptance run decoded all 3,357 pictures with no sequence
gaps or audio recovery events. Average decode improved from 66,462.1 to
66,014.3 us (0.67%), complete work from 95,111.5 to 94,585.2 us (0.55%) and
observed rate from 13.475 to 13.535 fps.

Because skipped blocks are now copied during compact row commit, their
temporary byte-planar prediction was dead data. Removing that unpack preserved
the decoded hash and reduced compact-reference copy calls by exactly 120
(40 skipped macroblocks times Y/U/V). C QEMU improved from 2,629,072 to
2,619,812 cycles/picture (0.35%); C++ measured 2,619,790.

All three physical profile trials were bit-for-bit identical. Motion
compensation improved from 7,875,511 to 7,588,039 cycles/picture (3.65%) and
complete decode from 14,956,841 to 14,700,677 cycles/picture (1.71%).
Decoder memory and heap headroom were unchanged.

The production build again completed all 3,357 pictures with no sequence gaps,
rebuffers, underruns or inserted silence. End-to-end timing did not improve in
this run: observed rate was 13.522 fps, average decode 66,137.4 us and complete
work 94,655.1 us, versus 13.535 fps, 66,014.3 us and 94,585.2 us in the
preceding run. The change is retained on the three deterministic physical
decoder profiles rather than the noisier renderer/audio wall-clock result.

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
- [x] Measure motion compensation IRAM placement independently.
- [x] Place stage-profiled hot motion-compensation code in IRAM and keep byte-addressed
      working data in DRAM.
- [x] Track IRAM use, free heap and largest free block for every retained
      placement.

Disabling IRAM placement for motion compensation preserved the decoded hash
but increased complete decode from 15,528,990 to 18,256,396 cycles/picture
(17.56%) in two identical physical trials; the third differed by only 38
cycles. Motion compensation itself increased from 8,268,518 to 9,746,560
cycles/picture (17.88%), while flash-cache contention also slowed VLC and
IDCT. The retained IRAM placement costs 26,208 bytes: IRAM grows from 45,195
to 71,403 bytes (34.48% to 54.48%) and flash code shrinks by the same amount.
Runtime decoder memory (194,056 bytes), free heap (104,112 bytes) and largest
free block (102,400 bytes) are unchanged. The existing enabled default is
therefore a large confirmed win with 59,669 bytes of IRAM still free.

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

- [x] Print recent raw UART context when physical metric capture detects a
      frame-sequence gap.
- [x] Remove matching cached records from `crc32.txt` atomically when
      `HLVDELETE` removes a test file, and avoid duplicate records on upload.
- [x] Make the uploader completion parser accept an exact valid response
      before unrelated bytes emitted during the UART baud transition.

The recent-line dump distinguished a selected BPV1 file failure from an
apparent MPEG-4 firmware reset at frame 995 without changing the SD card or
firmware. C and C++ capture tools emit the same bounded 40-line context only
on failure.

The parser now accumulates raw UART bytes and accepts only the complete
expected protocol/version/size/CRC/name record. Bytes after that exact record
are ignored, so transition noise cannot extend the filename. A regression
test reproduces the observed invalid-byte suffix; all four uploader tests
pass, and the C and C++ uploader implementations remain identical.

The C and C++ firmware now rewrite `crc32.txt` through a flushed and synced
temporary file, replace the previous index through a backup rename, and drop
all prior records for the affected filename before optionally writing its one
current record. Both production firmware variants build successfully. On the
physical ESP32, uploading the 38-byte `crc_index_probe.txt` grew the index from
3604 to 3636 bytes; uploading the same file again left it at 3636 bytes.
`HLVDELETE` removed the file and returned the index to exactly 3604 bytes.
Fresh listings after upload and deletion contained neither `crc32.txt.part`
nor `crc32.txt.bak`; the retained MPEG-4 test AVI remained 32800102 bytes and
`play.txt` remained 37 bytes.
