# ESP32 cross-codec optimization TODO

This plan tracks decoder and renderer optimizations that may transfer between
the HLV, DivX 3, MPEG-1, H.263, BPV and MJPEG playback paths on the original
dual-core ESP32-2432S028 board.

Every candidate is an A/B experiment. Keep it only when it preserves decoded
output and produces a repeatable benefit on the physical ESP32. QEMU is the
first correctness and instruction-count gate, but it cannot model SPI flash
cache misses, DMA contention or the exact Xtensa pipeline.

## Common acceptance process

For every candidate:

1. Record the current QEMU and physical-board baseline from the same build,
   stream, frame range and clock configuration.
2. Apply one focused change.
3. Rebuild and run native tests where available.
4. Run the matching QEMU benchmark and verify frame count and output hash.
5. Build, flash and run at least three physical-board trials.
6. Compare average, p50, p95 and maximum decode/render time as applicable.
7. Keep and commit the change only when the result is repeatable and no
   correctness, memory or binary-size regression outweighs the gain.
8. Remove rejected code and record the measured result in this document.

Shared infrastructure and correctness criteria may be reused. Hot bitreader,
VLC and transform implementations should remain codec-specific because their
cache widths, VLC or Exp-Golomb semantics and inlining requirements differ.

## DivX 3

- [x] Establish a fresh QEMU and physical-board baseline.
- [x] Place only `bit_read`, `bits_read` and `decode_vlc_index` in IRAM.
- [x] Verify the QEMU output hash and record QEMU cycles.
- [x] Run three physical-board trials and measure the flash-cache-sensitive
      effect.
- [x] Keep or reject the IRAM placement and document IRAM consumption.

Do not place the complete approximately 6 KiB `decode_inter_picture` path in
IRAM. QEMU is not expected to predict the flash-cache benefit: selective HLV
IRAM placement was QEMU-neutral but improved the physical board by 14.74%.

The selective placement was retained. The portable 256x144 regression remained
pixel exact. Both QEMU variants produced hash `1463ec78314286a3` and averaged
1,474,119 guest cycles per 320x240 frame. Three 300-frame board runs for each
variant were deterministic:

| DivX 3 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Flash helpers | 39,274.0 us | 43,250 us | 64,392 us | 80,304 us | baseline |
| IRAM helpers | 38,685.8 us | 42,356 us | 63,345 us | 79,844 us | accepted |

The board decode average improves by 1.50%. The change moves 496 bytes from
Flash code to IRAM, leaves DRAM unchanged, and increases the padded application
binary by 16 bytes. All six board runs decoded frames 1 through 300 without
sequence gaps, audio rebuffers, missing samples or inserted silence.

## MPEG-1

- [x] Establish a fresh QEMU and physical-board baseline.
- [x] A/B-test IRAM placement for the bitreader and hot VLC helpers.
- [x] Add a small direct lookup for the most frequent coefficient prefixes.
- [x] Verify bit-exact output, QEMU cycles and physical-board timing after each
      independent change.
- [x] Keep only individually justified changes and record IRAM/table size.

The decoder already has a 32-bit reader and 16-bit lookahead in
`third_party/pl_mpeg/pl_mpeg.h`. The remaining candidates are fewer flash-cache
misses and less traversal of frequent DCT coefficient VLCs, not another generic
bitreader.

The selective IRAM experiment was retained. It covers the buffer availability,
32-bit cache refill/read and VLC traversal helpers. The complete 3,359-frame
compact regression retained checksum `6bc59309b0d7dc23`. The 60-frame QEMU
benchmark retained hash `14d6bd4e019da037`; its average changed only from
1,753,290 to 1,753,288 guest cycles per frame.

| MPEG-1 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Flash helpers | 44,478.2 us | 44,421 us | 59,702 us | 67,226 us | baseline |
| IRAM helpers | 43,833.3 us | 43,779 us | 58,696 us | 64,737 us | accepted |

These values are the medians of three 300-frame physical-board runs per
variant. The average improves by 1.45% with no frame-sequence gaps. The change
uses 1,096 additional IRAM bytes, removes 1,020 bytes of Flash code, leaves
DRAM unchanged and increases the padded application binary by 80 bytes.
Both variants report the same known audio underrun pattern because this
240x180 stream is 30 fps while total decode/render work exceeds its frame
period.

The six-bit coefficient-prefix lookup was also retained. It resolves 59 of 64
prefixes without walking the full VLC tree and falls back for the remaining
five. The complete 3,359-frame compact regression again retained checksum
`6bc59309b0d7dc23`. QEMU retained hash `14d6bd4e019da037` and improved from
1,753,288 to 1,710,092 guest cycles per frame, or 2.46%.

| MPEG-1 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| IRAM helpers | 43,833.3 us | 43,779 us | 58,696 us | 64,737 us | baseline |
| IRAM helpers + prefix lookup | 42,656.1 us | 42,658 us | 57,059 us | 62,005 us | accepted |

These are again medians of three 300-frame board runs. The lookup improves
physical decode time by another 2.69%, or 4.10% cumulatively relative to the
original Flash-helper baseline. It adds 256 bytes of read-only table data and
116 bytes of Flash code, leaves IRAM and DRAM unchanged, and increases the
padded application binary by 368 bytes. All trials had no frame-sequence gaps
and reproduced only the stream's known audio-underrun pattern.

## H.263

- [x] Establish a fresh QEMU and physical-board baseline.
- [x] A/B-test selective IRAM placement for `BitstreamFillCache`.
- [x] Independently A/B-test the hot VLC/dequant helpers.
- [x] Independently A/B-test the hottest IDCT kernel.
- [x] Verify output and record QEMU, board and IRAM-size results.

PacketVideo already uses a 32-bit cache, VLC tables, byte-sized prediction,
fused IDCT-add and variable-complexity IDCT. Do not duplicate the DivX
algorithmic changes without profiler evidence.

The isolated `BitstreamFillCache` placement was retained. The complete host
profile smoke test passed, and both QEMU variants produced hash
`e2f9d3b5a212be20` with an identical average of 428,624 guest cycles per
320x240 frame. Three 300-frame physical-board runs per variant gave:

| H.263 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| Flash cache refill | 18,114.6 us | 17,473 us | 21,173 us | 68,993 us | baseline |
| IRAM cache refill | 16,706.5 us | 15,836 us | 19,677 us | 66,755 us | accepted |

The physical decode average improves by 7.77%. The change moves 272 bytes from
Flash code to IRAM, leaves DRAM and total image size unchanged, and preserves
all 300 frame indices with no audio rebuffers, underrun samples or inserted
silence.

The active short-header `VlcDequantH263IntraBlock_SH` path was then tested
independently and retained. QEMU again produced hash
`e2f9d3b5a212be20` and 428,624 average guest cycles per frame with and
without the placement.

| H.263 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| IRAM cache refill | 16,706.5 us | 15,836 us | 19,677 us | 66,755 us | baseline |
| IRAM refill + VLC/dequant | 15,900.1 us | 15,159 us | 19,155 us | 64,239 us | accepted |

The additional board improvement is 4.83%, or 12.22% cumulatively relative
to the all-Flash H.263 baseline. It moves another 1,240 bytes into IRAM,
removes 1,208 bytes of Flash code, leaves DRAM unchanged and adds 32 bytes to
the application image. Only the active 1.2 KiB H.263+ intra path is placed;
the unused approximately 4 KiB MPEG-4 intra helper remains in Flash.

Finally, the active intra-only IDCT dispatcher, fallback and variable-
complexity row/column kernels were tested as one isolated transform unit and
retained. Inter prediction IDCT remains in Flash. QEMU remained bit exact at
hash `e2f9d3b5a212be20` and 428,624 average guest cycles per frame. The final
full host test passed all supported H.263/H.263+, 3GP/AVI and AMR/PCM profiles.

| H.263 variant | Decode average | P50 | P95 | Maximum | Decision |
| --- | ---: | ---: | ---: | ---: | --- |
| IRAM refill + VLC/dequant | 15,900.1 us | 15,159 us | 19,155 us | 64,239 us | baseline |
| Above + intra IDCT | 14,438.0 us | 13,753 us | 17,596 us | 63,278 us | accepted |

The IDCT placement improves board decode by another 9.20%, or 20.30%
cumulatively from the original H.263 baseline. It moves 3,368 more bytes into
IRAM, removes 3,292 bytes of Flash code, leaves DRAM unchanged and adds 76
bytes to the image. Across all three H.263 changes, the cost is 4,880 IRAM
bytes and 108 image bytes; 64,661 IRAM bytes remain free. Every physical run
decoded 300 consecutive frames with no playback or audio errors.

## BPV

- [x] Establish a fresh QEMU and physical-board decode/render baseline.
- [x] Build a 64x16 RGB565 palette lookup table only when a keyframe changes
      the palette.
- [x] Use the cached table in the render path.
- [x] Verify output and measure decode, render and total frame time in QEMU and
      in three physical-board trials.
- [x] Keep the change only if the renderer benefit justifies 2 KiB of memory.

The deterministic QEMU render benchmark keeps RGB565 hash
`d761ba3e770d64eb` and improves a complete 320x240 conversion from 314,621 to
256,999 guest cycles, or 18.31%. Portable decoder and C-encoder compatibility
tests also pass.

Three 300-frame physical-board trials of the same 320x240 BPV v4 stream improve
median render average from 16,827.5 to 16,665.4 us, or 0.96%, and median total
work from 39,315.7 to 39,160.1 us, or 0.40%. Decode remains within 1.5 us of
baseline. All 900 frames play at 30 fps with no gaps, display skips, rebuffers
or audio underruns. The smaller full-player gain is expected because its render
timer also includes display-buffer and SPI/DMA waiting.

The retained table adds 2,056 DRAM bytes and 388 Flash-code bytes. IRAM and
Flash data are unchanged; 139,668 DRAM bytes remain free. BPV v4/v5 rebuild
the cache only when rendering a keyframe whose decode installed a new palette.
The allocation-free legacy renderer and portable RGB24 path remain available.

## MJPEG

- [x] Establish a fresh QEMU/host correctness baseline and physical-board
      decode/render baseline.
- [x] Remove the intermediate RGB565 strip copy by writing completed MCU rows
      directly into a display DMA buffer.
- [x] Verify the ROM TJpgDec direct-to-DMA path independently.
- [x] Benchmark the `esp_new_jpeg` backend against ROM TJpgDec.
- [x] Prefer direct RGB565 output when supported by the selected backend.
- [x] Run correctness checks and three physical-board trials for each
      retained backend/output combination.
- [x] Record memory, flash/IRAM size, decode, render and total frame timing.

DivX bitreader and sparse-IDCT changes do not apply to MJPEG because the current
ROM entropy decode and IDCT run inside TJpgDec. Both useful independent
candidates were measured: direct display-DMA output for the ROM path and the
official `esp_new_jpeg` 1.0.2 block decoder with direct RGB565 output.

The deterministic 12-frame QEMU benchmark retained a stable output per backend.
Removing the ROM strip copy improved 3,512,537 to 3,489,447 guest cycles per
frame, or 0.66%. The new backend then reduced this to 1,859,927 cycles, a
further 46.70%. Comparing all 921,600 output pixels found that every RGB565
component differed from ROM by at most one quantization step. The aggregate
absolute errors were 0.0430 red, 0.1383 green and 0.0994 blue steps per pixel.

Three 300-frame physical-board trials of the 320x240, 30 fps MJPEG stream gave:

| MJPEG path | Decode average | Decode P50 | Render average | Work average | Observed fps |
| --- | ---: | ---: | ---: | ---: | ---: |
| ROM + copied strip | 41,031.1 us | 70,239 us | 986.9 us | 49,990.4 us | 19.648 |
| ROM + direct DMA | — | — | 750.7 us | 49,055.4 us | 19.953 |
| `esp_new_jpeg` + direct DMA | 24,285.6 us | 33,630 us | 1,042.4 us | 33,333.9 us | 29.993 |

The selected Player path is `esp_new_jpeg` with 16-row RGB565 blocks written
straight into the two display DMA buffers. It improves decoder average by
40.81%, decode P50 by 52.12%, and observed presentation rate by 52.65% relative
to the original ROM strip-copy path. Each new-backend trial presented 300
consecutive frames with no sequence gaps, audio rebuffers, underrun samples or
inserted silence. Display skips fell from 141 to 93 because 207 of the 300
packets still arrived after their nominal frame deadline.

After this A/B decision, the superseded ROM TJpgDec implementation, callbacks,
work buffer and QEMU backend switches were removed. The measurements above are
retained as the historical acceptance evidence. The clean accelerated-only
QEMU build produces the same hash in 1,859,905 cycles per frame.

The Player no longer allocates the 10,240-byte intermediate strip or the
4,096-byte ROM work area. The compact QEMU configuration reports 249,904 free
heap bytes, 14,344 more than the first new-backend integration and 2,104 more
than the ROM direct-DMA benchmark; its largest free block remains 118,784
bytes. The linked component increases Flash code by 59,020 bytes, Flash data
by 2,656 bytes, IRAM by 7,108 bytes and static DRAM by 3,304 bytes relative to
the pre-MJPEG experiment build. The final image is 705,723 bytes with 57,553
IRAM and 136,364 static DRAM bytes remaining.

## Priority order

1. Selective IRAM placement for DivX, MPEG-1 and H.263.
2. Frequent MPEG-1 coefficient VLC prefixes.
3. BPV palette-to-RGB565 rendering.
4. MJPEG direct-to-DMA output and alternative JPEG backend.

HLV and H.263 are already close to algorithmic saturation. Common code should
remain limited to compact frame-buffer operations, RGB565 conversion,
benchmark infrastructure and bit-exact verification criteria.
