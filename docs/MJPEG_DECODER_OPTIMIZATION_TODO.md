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

- [ ] Process two or four RGB888 pixels per loop iteration and compare the
      generated Xtensa assembly with the current scalar loop.
- [ ] A/B test small RGB565 component lookup tables against packed arithmetic.
- [x] Place only proven hot output/conversion code in IRAM and record
      instruction-cache effects.
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
