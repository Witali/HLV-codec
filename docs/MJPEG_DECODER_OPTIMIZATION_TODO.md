# MJPEG decoder optimization TODO

This list covers AVI/MJPEG demuxing, JPEG decoding and RGB565 presentation on
the original dual-core ESP32-2432S028 board. Optimize against repeated
measurements from the physical board. Host and QEMU measurements are useful
for correctness, but are not substitutes for the ESP32 cycle count, internal
SRAM pressure, SD latency or SPI display timing.

The primary goal is stable 24 fps playback without changing the JPEG stream or
reducing image quality. Encode-side quality trade-offs are listed separately
and must not be mixed into decoder benchmarks.

## Current baseline

Test stream:
`BigBuckBunny_1080p_mjpeg_q5_native-fps_320x180.avi`.

- baseline JPEG, YUV420, 320x180, 24/1 fps;
- 14,315 video frames;
- PCM_U8 mono audio at 16 kHz;
- frame-time budget: 41,667 us;
- original ESP32 at 240 MHz, no PSRAM;
- SD SPI at 40 MHz and ST7789 SPI at 80 MHz.

After enabling a separate FatFs cache per descriptor and retaining FastSeek, a
3,000-frame physical-board run completed without packet errors, frame-number
gaps or audio underruns:

| Stage | Average | p50 | p95 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Video SD read | 5,500 us | 7,731 us | 10,149 us | 99,272 us |
| Logged JPEG decode | 35,204 us | 49,149 us | 63,263 us | 254,589 us |
| Logged render | 1,009 us | 1,420 us | 1,903 us | 24,832 us |
| Complete work | 41,713 us | 52,962 us | 73,666 us | 309,588 us |

The decode and render averages above include 1,037 frames that were deliberately
not decoded or displayed after they were already late. From the remaining
1,963 displayed frames, the estimated conditional averages are approximately:

- JPEG decode: 53,801 us;
- render submission/waiting: 1,541 us;
- effective displayed rate: about 15.6 fps.

The current bottleneck is therefore JPEG decompression and colour conversion,
not normal SD throughput. SD tail latency still matters for smoothness.

The historical baseline used the ESP32 ROM TJpgDec implementation:

1. the complete compressed frame is read into a bounded packet buffer;
2. ROM TJpgDec produces RGB888 MCU blocks;
3. `jpegOutput()` converts every pixel to RGB565;
4. a complete 16-row RGB565 strip is assembled;
5. `renderMjpegStrip()` copies that strip into one of two display DMA buffers.

That ROM decoder had fixed RGB888 output and the basic `JD_FASTDECODE=0`
configuration. It has now been replaced by `esp_new_jpeg` block RGB565 output
and removed from the codebase. The player still opens the AVI twice: one
descriptor scans for video chunks and another independently scans the same
interleaved chunks for audio.

The current firmware link report uses 61,620 bytes of static DRAM and reports
119,116 bytes remaining before runtime allocations and task stacks. A single
320x180 RGB565 framebuffer needs 115,200 bytes, so full-frame double buffering
is not a viable design on this board.

## Acceptance criteria

Every retained optimization must pass all applicable checks:

- [ ] Decode all 14,315 MJPEG frames without packet or JPEG errors.
- [ ] Preserve dimensions, frame order, frame count, rational frame rate and
      every PCM sample.
- [ ] Report zero audio rebuffers, missing samples and inserted silence.
- [ ] Run at least three reset-to-reset 3,000-frame hardware trials.
- [ ] Record average, p50, p95, p99 and maximum cycles/time for SD, header
      preparation, entropy/IDCT, colour packing, DMA waiting and total work.
- [ ] Record displayed frames separately from packets that were intentionally
      skipped before decode.
- [ ] Record free heap and largest free 8-bit/DMA-capable block before opening
      the file and after decoder allocation.
- [ ] Keep an optimization only when the speedup is repeatable and memory
      headroom remains safe.

Changes inside the existing ROM-TJpgDec output path must retain the complete
RGB565 frame hash. An alternative standards-compliant JPEG decoder may have
legal IDCT rounding differences; in that case compare against the desktop
reference, record maximum pixel error and RGB PSNR, and inspect representative
frames before accepting it.

The full-player target is:

- at least 23.9 presented frames/s for the 24 fps stream;
- zero display skips in each 3,000-frame acceptance run;
- decoder-only p95 below 40 ms;
- complete-work p95 below the 41.667 ms frame budget after read/decode overlap.

## Priority 0: improve the measurement

- [ ] Add cycle counters around `jd_prepare()`, `jd_decomp()`, `jpegInput()`,
      RGB888-to-RGB565 packing, `display.acquireBuffer()` and DMA submission.
- [ ] Count callback invocations, copied input bytes and converted pixels.
- [ ] Derive entropy/IDCT time by subtracting measured callbacks from
      `jd_decomp()` rather than treating the complete call as one cost.
- [ ] Add p99 to the physical-board collector.
- [ ] Prepare a fixed short corpus containing calm frames, high-detail frames,
      the largest JPEG packet and the previously investigated packet 1528.
- [x] Capture three baseline runs before changing the decoder.
- [ ] Disable per-frame UART output for production measurements after the
      counters have been collected. At 460800 baud it consumes roughly
      0.8-1.0 ms of wall time per frame even though timestamps are captured
      before printing.

## Priority 1: benchmark `esp_new_jpeg`

Espressif's `esp_new_jpeg` supports the original ESP32, baseline JPEG, direct
RGB565 output, reusable stream handles and block decoding. Its published
performance numbers cover newer ESP32-S2/S3 chips, so do not extrapolate them
to this board.

- [x] Pin one reviewed `esp_new_jpeg` version in the ESP-IDF component
      manifest; start with version 1.0.2.
- [x] Build a decoder-only A/B harness that feeds the exact same compressed
      packets to ROM TJpgDec and `esp_new_jpeg`.
- [x] Use RGB565 little-endian block output; do not allocate a complete frame.
- [x] Reuse the decoder handle and aligned output block across all frames.
- [x] Compare decode cycles, heap use and output pixels.
- [ ] Verify restart-marker handling and all 14,315 frames before player
      integration.
- [x] Remove ROM TJpgDec after the new backend passes QEMU pixel comparison,
      repeated physical-board runs and explicit acceptance.

The completed short-corpus A/B used 12 original 320x240 compressed frames.
`esp_new_jpeg` 1.0.2 with block RGB565 output averaged 1,859,927 QEMU guest
cycles per frame versus 3,489,447 for ROM direct output. All RGB565 channels
were within one quantization step of ROM. Three 300-frame board runs averaged
24,285.6 us decode time at 29.993 observed fps. The longer 14,315-frame
acceptance run above remains open.

The old decoder, RGB888 conversion callback, 4 KiB work buffer and ROM-specific
QEMU switches were then deleted. The accelerated-only QEMU build retains hash
`6b9ad099d4648dfe` at 1,859,905 cycles per frame; the historical A/B values
remain recorded in this document.

References:

- <https://components.espressif.com/components/espressif/esp_new_jpeg/versions/1.0.2/readme>
- <https://components.espressif.com/components/espressif/esp_jpeg/versions/1.3.1/readme>

## Priority 2: write RGB565 directly into display DMA strips

- [x] Let the renderer provide the current writable DMA strip to the decoder
      when a JPEG MCU row begins.
- [x] Convert ROM RGB888 blocks directly into that DMA strip.
- [x] Submit the strip when the rightmost MCU completes it.
- [x] Remove the separate 10,240-byte MJPEG RGB565 strip allocation from the
      selected Player path.
- [x] Remove the full-strip `memcpy()` in `renderMjpegStrip()`.
- [x] Preserve two-buffer asynchronous display transfer and error propagation.
- [x] Confirm the complete ROM RGB565 hash is unchanged.

Expected benefit is modest, initially estimated at 1-2 ms per displayed frame,
but this also releases 10 KiB for packet read-ahead or an alternative decoder.
Measure rather than relying on the estimate.

## Priority 3: replace the two-reader AVI path with one demuxer

- [ ] Read `movi` sequentially through one buffered `FILE`.
- [ ] Dispatch `00dc` JPEG payloads to the video packet queue and `01wb` PCM
      payloads to the audio stream buffer.
- [ ] Remove the second complete scan of interleaved AVI chunks.
- [ ] Avoid `fseek()` for ordinary sequential payload consumption.
- [ ] Keep FatFs per-file cache and FastSeek enabled for file opening,
      validation, seeking and loop restart.
- [ ] Preserve the existing audio preroll and DMA sample clock.
- [ ] Measure SD bus occupancy, read p95/p99 and audio queue depth.

This primarily reduces SD contention and latency tails. It will not by itself
make a 54 ms JPEG decode fit a 41.7 ms frame budget.

## Priority 4: overlap read and decode across the two cores

- [ ] Add two bounded compressed-packet buffers sized from the AVI index.
- [ ] Pin JPEG decode to CPU1.
- [ ] Let CPU0 demux the next AVI chunks and maintain the PCM queue while CPU1
      decodes the current JPEG.
- [ ] Queue packets by frame number and preserve presentation order.
- [ ] Avoid sharing a mutable decoder work area between cores.
- [ ] Record task migration, queue waits and per-core cycle use.
- [ ] Reject the design if the second packet buffer leaves insufficient
      largest-free-block headroom.

Read/decode overlap can hide approximately the normal 5.5 ms video read time
and prevents the CPU0 audio reader from pre-empting JPEG decode. It does not
parallelize entropy decoding within one JPEG frame.

## Priority 5: optimize the remaining hot loops

- [x] Place only proven hot output/conversion code in IRAM and record
      instruction-cache effects.
- [x] Add a first-party Xtensa DC-only IDCT shortcut and retain the original
      `esp_new_jpeg` kernel as the fallback.
- [x] Separate header/process/correctness-callback cycles in the benchmark so
      RGB565 hashing does not dilute decoder-only deltas.
- [x] Extend the Xtensa IDCT with exact one-column and two-column reduced-row
      paths; retain the original kernel for every unmatched coefficient mask.
- [x] Extend the reduced-row IDCT A/B to natural-order column two; reject the
      repeatable slowdown and keep the two-column limit.
- [x] A/B a three-byte marker-free Huffman reservoir prefill at each decoder
      entry; reject the wrapper overhead and retain the current byte path.
- [x] Inspect specialization of the aligned, restart-free YUV420/RGB565LE MCU
      loop; do not fork the complete prebuilt kernel without a source seam.
- [x] Reject paired 32-bit RGB565 stores as a standalone change: packing each
      pair needs `SLLI`, `OR` and `S32I` instead of two `S16I` instructions.
- [x] A/B exact `MUL16S` chroma products independently from tables; reject the
      DSP substitution because it has zero cycle benefit on the ESP32.
- [x] A/B reuse of variable-size Huffman allocations while still rebuilding
      the frame-specific canonical and lookup tables; reject the negligible
      result and retain normal allocation.
- [x] Replace the aligned zero-128 coefficient `memset` with a bit-exact
      unrolled IRAM clear and keep libc for every unmatched call.
- [x] A/B sparse coefficient clearing after the retained fast-clear wrapper;
      reject the physical-board slowdown and remove the candidate.
- [x] Reject a wider primary VLC lookup for the current stream: no measured DC
      symbols and only 2.737% of AC symbols exceed eight Huffman bits.
- [ ] A/B a fixed 16x16 YUV420-to-RGB565LE kernel for the 320-pixel output
      stride. Unroll only the eight four-pixel groups, retain the exact
      fixed-point products and clipping-table semantics, and keep the original
      library function as the fallback for every other geometry.
- [ ] Independently A/B two packed 256-entry U/V contribution tables after the
      fixed-geometry colour result is known. Charge the 2 KiB internal-DRAM
      cost separately and do not combine both changes in the first build.
- [ ] Prototype a complete paired Huffman table-builder/decoder replacement
      with packed 16-bit `{nbits,symbol}` primary entries and marker-safe
      multi-byte refill at every refill site. Do not repeat the rejected
      entry-only refill wrapper.
- [ ] Measure a fixed aligned, restart-free YUV420 MCU call path only after the
      isolated colour kernel. Preserve the original 0x5ea-byte process
      function for restart-coded, rotated, scaled and edge MCUs.
- [ ] Instrument the first excluded coefficient pair for reduced-IDCT
      fallbacks, reorder the checks by measured rejection frequency, and keep
      the order only if both QEMU and COM8 improve.
- [ ] Test a larger TJpgDec input buffer with a source-built decoder.
- [ ] Test source-built TJpgDec `JD_FASTDECODE=2` only after measuring its
      approximately 65.5 KiB work-buffer cost on the real firmware.
- [ ] Record cycle and operation-count deltas for every experiment.

The IRAM experiment retained four active `esp_new_jpeg` functions: Huffman
decode, the YUV420 block process kernel, the top-level non-rotated process
kernel and YUV420-to-RGB565LE packing. The public component does not ship the
decoder implementation, only API headers, tests and per-SoC prebuilt archives.
Its ESP32 archive does retain object boundaries, function sections, symbols and
DWARF data. The build therefore makes a derived archive with only the selected
sections renamed to `.iram1.*`, leaving the managed archive unchanged.

QEMU ON/OFF builds remained bit exact with hash `6b9ad099d4648dfe` and the same
1,859,905 guest cycles per frame. Three 300-packet physical-board trials found
a 35,157.0 to 34,912.5 us median reduction per actually decoded frame (0.70%).
Paired common-frame gains were 267.5-307.7 us in all trials. The faster path
decoded 209 rather than 207 packets before their deadlines, reducing display
skips from 93 to 91. It costs 3,720 IRAM bytes, leaves 53,833 IRAM bytes free
and is retained by default. Set `MJPEG_HOT_IRAM=OFF` for the control build.

The retained IDCT experiment adds an IRAM assembly wrapper around
`idct_block_8_8`. It checks whether all 63 AC coefficients are zero, emits the
same rounded/clipped sample directly into the 8x8 block with 32-bit stores,
and calls the unchanged library kernel for every other block. A complete
60-frame QEMU run remained bit exact (`436f6b344bed074e`) and improved average
guest cycles from 1,832,549 to 1,771,053 (3.36%). Seven physical-board runs
retained the same hash and reduced average decode time from 39.542 to
38.509 ms per frame (2.61%); P50 improved by 2.73%. The optimized path is
enabled by default with `MJPEG_OPTIMIZED_IDCT=ON`.

The retained reduced-row extension checks whether all coefficients outside
natural-order DCT columns zero and one are empty. It transforms only the
occupied columns and uses exact reduced row equations; every other block still
calls the original kernel. The 60-frame QEMU A/B remained bit exact
(`436f6b344bed074e`) and improved 1,771,107 to 1,746,716 total guest cycles
per frame (1.38%). Five identical reset-to-reset COM8 trials improved
9,243,179 to 9,133,439 total cycles (1.19%) and 7,776,263 to 7,666,750
decoder-only cycles (1.41%). Heap and largest-free-block values were
unchanged. `MJPEG_IDCT_REDUCED_ROWS=ON` is retained by default and can be
disabled independently while keeping the DC-only wrapper.

The RGB565LE kernel has two chroma products whose inputs and constants fit
signed 16-bit operands. A temporary derived-archive patch replaced only those
two `MULL` instructions with `MUL16S`; it did not modify the managed component.
The raw-object variant preserved relocations, decoded all 60 QEMU frames and
retained hash `436f6b344bed074e`. QEMU instruction counts were identical, as
expected. Five physical-board reset trials then matched the `MULL` control
exactly at 9,133,439 total and 7,666,750 decoder-only cycles per frame.
The substitution was removed. Chroma contribution tables would exchange the
same two products for DRAM loads while costing at least 2 KiB, so they are not
retained either.

The retained coefficient-clear wrapper recognizes only aligned
`memset(buffer, 0, 128)` calls and emits 32 unrolled `S32I` stores. Every
unmatched call uses the original libc implementation. It costs 118 bytes of
IRAM text plus a four-byte literal and no heap. The 60-frame QEMU A/B remained
bit exact (`436f6b344bed074e`) and improved 1,746,716 to 1,739,381 total guest
cycles per frame (0.42%). Five identical COM8 reset trials improved 9,133,439
to 9,073,825 total cycles (0.65%) and 7,666,750 to 7,607,262 decoder-only
cycles (0.78%). Heap and largest-free-block results were unchanged.
`MJPEG_FAST_COEFFICIENT_CLEAR=ON` is retained by default.

A bounded temporary allocation pool reused all 218 variable-size Huffman
allocations in the 60-frame corpus while still rebuilding every frame's
canonical and lookup tables. It preserved all frames and hash
`436f6b344bed074e`, but improved total QEMU cycles by only 558 (0.03%). Five
identical COM8 trials improved total time by 0.029% and decoder-only time by
0.037%, while permanently consuming another 608 bytes of heap. This is below
measurement significance and worsens memory headroom, so the pool was removed.

The sparse-clear experiment made the DC-only and reduced-row IDCT paths clear
only their proven coefficient footprint, then used a 16-entry pointer table to
skip the following 128-byte clear. It preserved the complete QEMU and COM8
hash. QEMU improved by only 939 cycles per frame (0.054%), while five
deterministic COM8 trials regressed from 9,073,825 to 9,079,944 total cycles
(0.067%) and from 7,607,262 to 7,613,242 decoder-only cycles (0.079%). It also
cost 64 bytes of DRAM. The sparse path and table were removed; the simpler
unrolled full clear remains.

The three-column reduced-row IDCT extension added a bit-exact even-term row
transform for blocks confined to natural-order DCT columns zero through two.
All 60 QEMU frames retained hash `436f6b344bed074e`, but average cycles
regressed from 1,739,381 to 1,741,689 (0.13%). Five COM8 reset trials retained
the same hash and memory readings while regressing from 9,073,825 to about
9,090,505 total cycles (0.18%) and from 7,607,262 to about 7,623,998
decoder-only cycles (0.22%). The additional mask checks and row arithmetic
cost more than the saved fallback work, so the extension was removed.

The marker-safe entropy experiment wrapped `jpeg_dec_huffman` and appended
three bytes to its existing 32-bit reservoir only when fewer than eight bits
remained, at least three bytes were available and none equalled `0xff`.
Markers, byte stuffing and tails retained the original path. The complete
QEMU hash stayed `436f6b344bed074e`, but total cycles regressed from 1,739,381
to 1,741,324 (0.11%) and decoder-only cycles from 1,407,085 to 1,409,036
(0.14%). Five deterministic COM8 trials retained the same hash and memory,
but regressed to 9,090,475 total cycles (0.18%) and 7,624,101 decoder-only
cycles (0.22%). The wrapper was removed. A true multi-site bulk refill would
require replacing all duplicated refill loops inside the complete 0x520-byte
prebuilt Huffman function; there is no separately linkable refill seam.

The selected backend path is already the dedicated
`jpeg_dec_proc_yuv420_0_block` kernel, not the general rotate/scale output
loop. It handles one 16x16 MCU with fixed YUV420 work areas and calls the
three Huffman groups, six IDCT blocks and RGB565LE conversion through internal
function pointers. Restart and edge checks are embedded in the 0x5ea-byte
prebuilt function. Hoisting those checks and converting indirect calls to
direct calls therefore requires maintaining a complete reconstructed private
decoder structure and kernel, not a small independently reversible assembly
replacement. No production fork is retained without source or a measurable
isolated patch point.

The 60-frame follow-up scan covers 108,000 coefficient blocks. DC-only blocks
account for 17.92%, 22.92% have non-zero coefficients only in DCT column zero,
and 32.93% use only columns zero and one. The decoder clears 13,824,000
coefficient bytes over the run (230,400 bytes per frame). All frames are
YUV420 with `2x2,1x1,1x1` sampling, dimensions aligned to complete 16x16 MCUs
and no restart interval. DQT, SOF and SOS are stable, but 58 of 60 DHT sets are
unique, so full header/table caching is not applicable to the current encoder
output.

Apply each unchecked experiment as an independently switchable A/B change.
Require the same complete RGB565 hash and frame count in QEMU, then repeat the
same baseline/candidate builds on COM8. Retain production code only when the
physical-board improvement is consistent, larger than run-to-run noise and
does not compromise heap, largest-free-block, IRAM or Flash limits. Record and
remove rejected candidate code.

The phase benchmark is retained. On QEMU, the current optimized decoder
averages 1,438,865 decoder-only guest cycles: 3,730 in header parsing, 35 in
geometry queries and 1,434,787 in `jpeg_dec_process`. The RGB565 correctness
hash costs another 332,242 cycles inside the historical total. Seven identical
COM8 runs report 7,776,263 decoder-only cycles (32.401 ms at 240 MHz), of which
7,734,619 are `jpeg_dec_process`; header parsing costs only 32,308 cycles.
Correctness hashing costs 1,466,916 cycles (6.112 ms). Subsequent decisions use
the decoder-only value while retaining the hash as acceptance evidence.

Espressif's published S3 comparison shows source TJpgDec with
`JD_FASTDECODE=2` improving approximately 52 ms to 46 ms, while direct RGB565
output can be slower than the ROM RGB888 path. Treat it as an experiment, not
an assumed upgrade.

## Encode-side experiments with a quality trade-off

The current test file is already baseline JPEG with YUV420 chroma subsampling.
The following are optional compatibility experiments and are not
quality-preserving decoder optimizations:

- [ ] Measure several JPEG quality levels against decode time, packet size and
      RGB PSNR.
- [ ] Test whether default versus optimized Huffman tables affect the ESP32
      entropy decoder.
- [ ] Consider restart intervals only if a tested decoder can exploit them.
- [ ] Keep frame rate, 320x180 dimensions and PCM track unchanged in each A/B
      comparison.

## Not currently justified

- Full RGB565 frame double buffering: two frames require 230,400 bytes before
  compressed packets, decoder work areas, audio and task stacks.
- Raising the CPU clock: the firmware already runs the ESP32 at 240 MHz.
- Raising the tested SPI clocks: SD is already at 40 MHz and the display at
  80 MHz; JPEG decompression is the measured dominant cost.
- Replacing ROM TJpgDec with `esp_jpeg` configured for RGB565 without an A/B
  test: Espressif's published comparison shows that configuration can be
  slower and its fast mode requires substantial RAM.
- Parallel decoding of two complete output frames: without PSRAM there is no
  safe memory budget to retain the second decoded RGB565 frame.

## Recommended execution order

1. Add granular cycle counters and capture three baseline runs.
2. Benchmark `esp_new_jpeg` block RGB565 output without changing playback.
3. Integrate direct-to-DMA block output for the winning decoder.
4. Implement the single-reader AVI demuxer.
5. Add two-buffer compressed read-ahead and CPU1 decode.
6. Disable production per-frame UART logging and run the complete acceptance
   matrix.
7. Attempt micro-optimizations only if the combined pipeline still misses the
   24 fps target.
