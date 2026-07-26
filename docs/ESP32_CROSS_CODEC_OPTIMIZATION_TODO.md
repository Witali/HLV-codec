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

- [ ] Establish a fresh QEMU and physical-board baseline.
- [ ] A/B-test selective IRAM placement for `BitstreamFillCache`.
- [ ] Independently A/B-test the hot VLC/dequant helpers.
- [ ] Independently A/B-test the hottest IDCT kernel.
- [ ] Verify output and record QEMU, board and IRAM-size results.

PacketVideo already uses a 32-bit cache, VLC tables, byte-sized prediction,
fused IDCT-add and variable-complexity IDCT. Do not duplicate the DivX
algorithmic changes without profiler evidence.

## BPV

- [ ] Establish a fresh QEMU and physical-board decode/render baseline.
- [ ] Build a 64x16 RGB565 palette lookup table only when a keyframe changes
      the palette.
- [ ] Use the cached table in the render path.
- [ ] Verify output and measure decode, render and total frame time in QEMU and
      in three physical-board trials.
- [ ] Keep the change only if the renderer benefit justifies 2 KiB of memory.

The current approximate split is 5.2 ms decode and 16.9 ms render, so renderer
work is more important than further decoder micro-optimization.

## MJPEG

- [ ] Establish a fresh QEMU/host correctness baseline and physical-board
      decode/render baseline.
- [ ] Remove the intermediate RGB565 strip copy by writing completed MCU rows
      directly into a display DMA buffer.
- [ ] Verify the ROM TJpgDec direct-to-DMA path independently.
- [ ] Benchmark the `esp_new_jpeg`/`esp_jpeg` backend against ROM TJpgDec.
- [ ] Prefer direct RGB565 or YUV output when supported by the selected backend.
- [ ] Run complete correctness checks and three physical-board trials for each
      retained backend/output combination.
- [ ] Record memory, flash/IRAM size, decode, render and total frame timing.

DivX bitreader and sparse-IDCT changes do not apply to MJPEG because the current
entropy decode and IDCT run inside the ESP32 ROM TJpgDec implementation. The
useful candidates are a controllable JPEG backend and eliminating the current
strip-buffer copy.

## Priority order

1. Selective IRAM placement for DivX, MPEG-1 and H.263.
2. Frequent MPEG-1 coefficient VLC prefixes.
3. BPV palette-to-RGB565 rendering.
4. MJPEG direct-to-DMA output and alternative JPEG backend.

HLV and H.263 are already close to algorithmic saturation. Common code should
remain limited to compact frame-buffer operations, RGB565 conversion,
benchmark infrastructure and bit-exact verification criteria.
