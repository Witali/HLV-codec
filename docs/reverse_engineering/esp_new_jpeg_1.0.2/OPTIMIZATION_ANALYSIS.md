# ESP_NEW_JPEG decoder optimization analysis

## What is already optimized

The decoder already contains several optimizations that should not be
reimplemented:

- Huffman decoding keeps a 32-bit reservoir in local registers and uses an
  8-bit direct lookup before the canonical slow path.
- `jpeg_dec_huffman` decodes the four Y blocks in one call for a YUV420 MCU.
- `yuv420_to_rgb565le` reuses one U/V sample for a 2x2 Y group and uses a
  1024-byte clipping table instead of per-channel clamp branches.
- `idct_block_8_8` is hand-written Xtensa assembly in IRAM and has an
  all-zero-AC shortcut for each input column.
- The current Player build already moves Huffman, the YUV420 block MCU loop,
  non-rotated dispatch and RGB565LE conversion into IRAM.

The optimized reference Player map used for the retained A/B runs reports:

| Hot section | Linked size |
| --- | ---: |
| `jpeg_dec_huffman` | 0x520 bytes |
| `jpeg_dec_proc_yuv420_0_block` | 0x5ea bytes |
| `jpeg_dec_process_0` | 0x150 bytes |
| `yuv420_to_rgb565le` | 0x1de bytes |
| Complete IDCT object | 0x1af0 bytes |

## Full nine-object Ghidra follow-up

The decoder-side archive pass now also covers `esp_jpeg_memory.c.obj` and
`esp_jpeg_version.c.obj`, for a total of 116 retained functions. Neither
reveals a useful decode hot spot:

- the memory object contains six short wrappers around
  `heap_caps_calloc_prefer`, `heap_caps_aligned_calloc` and `heap_caps_free`;
- the version object only returns the compile-time version string.

The allocation-reuse A/B already measured only a 0.037% decoder-only physical
gain while consuming 608 persistent heap bytes. The version function is not
called by the Player. These objects should therefore remain untouched.

The complete Ghidra instruction scan reinforces that the remaining work is in
four functions:

| Function | Static instructions | Notable operations |
| --- | ---: | --- |
| `jpeg_dec_huffman` | 514 | 49 conditional branches, 27 byte-load sites |
| `jpeg_dec_proc_yuv420_0_block` | 585 | 33 indirect-call sites over all paths |
| `idct_block_8_8` | 201 | 19 multiplies, eight clip-table loads |
| `yuv420_to_rgb565le` | 184 | two `MULL`, 12 clip loads and four stores per four-pixel inner iteration |

For a 320x240 YUV420 frame the selected block path processes 300 complete
16x16 MCUs. The RGB565 converter therefore executes 19,200 four-pixel inner
iterations: 230,400 clip-table byte loads, 76,800 halfword stores and 38,400
`MULL` instructions per frame, in addition to the two shift/add constant
products per iteration. The aligned MCU loop also performs three Huffman, six
IDCT and one colour call per MCU, or 3,000 indirect calls per frame.

The production Player measurement on the physical board over 900 consecutive
320x240/30 packets decoded 565 packets and skipped 335 late packets. Excluding
the deliberate `decode_us=0` records, decode latency was 36.054 ms average,
36.886 ms P50, 40.240 ms P95 and 42.203 ms P99. This remains above the
33.333 ms frame budget and justifies another isolated optimization round.

## Remaining candidates after the completed A/B work

### A. Fixed-geometry RGB565LE kernel

This is the best remaining isolated candidate. Wrap
`yuv420_to_rgb565le` and select a first-party Xtensa kernel only when the
runtime geometry is the current unscaled, non-rotated 16x16 YUV420 MCU with a
320-pixel destination stride. Keep the original function for every other
geometry.

First A/B only the structural specialization:

1. hard-code the 320-pixel row stride and 16x16 block dimensions;
2. unroll the eight four-pixel groups in each two-row pair;
3. retain the exact existing fixed-point products, clipping-table indexing and
   four 16-bit stores so the change has no numerical ambiguity;
4. keep the eight chroma-row iterations as a small loop to limit IRAM growth.

This removes repeated `w_h` loads, inner-loop comparisons, stack spills and
pointer reconstruction without changing colour arithmetic. It is materially
safer than replacing the whole MCU function.

As a separate second A/B, test two packed 256-entry contribution tables: one
for U-derived blue/green terms and one for V-derived red/green terms. Two
32-bit loads would replace the two `MULL` operations and both synthesized
constant-multiply sequences for each four-pixel group. The cost is 2 KiB of
internal DRAM. This is different from the rejected `MULL` to `MUL16S`
substitution: it removes all four chroma products, so it must be measured
rather than inferred from that zero-gain DSP experiment.

Do not combine the fixed-geometry and contribution-table changes in the first
build. Their effects and memory costs need independent results.

### B. Combined Huffman lookup and integrated refill

The primary eight-bit lookup is already the correct width for this stream:
only 2.737% of AC symbols miss it. The remaining avoidable cost is that a fast
hit loads bit length and symbol from separate byte arrays, while the same
marker-safe one-byte refill logic is duplicated throughout the 0x520-byte
function.

A source-compatible replacement of both `jpeg_dec_create_huffman_tbl` and
`jpeg_dec_huffman` could use one packed 16-bit `{nbits,symbol}` primary table
and integrate a two- or three-byte no-`0xff` refill at every refill site. The
entry-only refill wrapper already regressed, so another wrapper experiment is
not useful. This candidate requires a complete bit-exact entropy replacement
and private-state validation; it comes after the colour kernel.

### C. Fixed aligned MCU call path

For the current files, width and height are MCU-aligned and the restart
interval is zero. A fixed kernel could remove the restart checks and replace
the ten indirect calls per MCU with direct calls to the selected Huffman,
8x8-IDCT and RGB565LE functions. The potential saving is bounded by 3,000
call edges and 300 always-false restart checks per frame.

This still requires reproducing the private `jpeg_decoder_t` layout and most
of a 0x5ea-byte function, so it has substantially more maintenance and
correctness risk than the colour specialization. Do not proceed unless phase
measurements show the colour kernel is no longer the best isolated target.

### D. Reorder the reduced-IDCT rejection checks

The retained one-/two-column shortcut scans excluded coefficient pairs before
falling back. Instrument which excluded pair first rejects the 67.07% of
non-matching blocks, then order those loads by observed rejection frequency.
This preserves every transform equation and only changes early-exit order.
The likely gain is small, but implementation and bit-exactness risk are low.

### Candidates not worth revisiting

- `esp_jpeg_memory`, version lookup and output-geometry queries;
- header caching for this stream, because DHT sets change in 58 of 60 sampled
  frames and header parsing is only about 32,308 cycles;
- wider flat VLC tables;
- `MUL16S` substitution, paired RGB565 stores, sparse coefficient clearing,
  three-column reduced IDCT and the entry-only entropy prefill;
- `jpeg_dec_color_scale`, rotation kernels and edge-copy paths, because the
  current Player does not execute them.

## Recommended experiments

### 1. Add an IDCT row/DC-only shortcut — implemented

This is the best first algorithmic candidate.

The first IDCT pass skips an AC-empty column, but the second pass always runs
the complete 1-D transform for all eight rows. For a DC-only coefficient block,
the column shortcut leaves every row with only its first value non-zero, yet
the row pass still executes all odd/even multiplications and eight clipping
lookups.

Add a bit-exact row shortcut that detects `row[1] | ... | row[7] == 0`, applies
the same rounding and scaling as the full path, and writes the repeated clipped
sample. Entropy instrumentation should also count DC-only blocks so the gain
can be predicted for each test video.

The retained implementation is ABI-compatible and replaces only the
`idct_block_8_8` call edge. A first-party IRAM assembly wrapper recognizes a
fully DC-only block, writes its exact repeated result, and reaches the
unchanged library function through GNU ld `--wrap` for every other block.
Rotation and scaled variants remain untouched.

The complete 60-frame QEMU A/B run retained RGB565 hash
`436f6b344bed074e` and improved average guest cycles by 3.36%. Seven repeated
physical ESP32 runs retained that hash and improved average decode time by
2.61% (39.542 to 38.509 ms); P50 improved by 2.73%. This exceeds run-to-run
noise and is enabled by default.

### 2. Specialize the interior YUV420/RGB565LE MCU loop

The general block function checks restart state and image edges inside the MCU
loop and calls six IDCTs plus color conversion through function pointers. The
normal full MCU does:

- one Huffman call for four Y blocks;
- one Huffman call each for U and V;
- four Y and two UV IDCT calls;
- one color conversion call.

Select a dedicated no-restart, no-rotation, full-size YUV420/RGB565LE interior
kernel once during decoder setup. Use direct calls for the fixed IDCT and color
functions. Keep the existing function as a fallback for restart-coded input,
right edges, the final partial row, scaling, clipping and rotation.

This is safe for both 320x240 and 320x180: 320 is MCU-aligned, while a 320x180
file uses the specialized interior loop and retains the original bottom-edge
path.

### 3. Test a fast entropy refill and compact second-level VLC table

The existing bit reservoir is good, but it refills one byte at a time and
duplicates byte-stuffing/marker handling at several inlined sites.

Two independent A/B variants are worth testing:

1. Append two or three bytes at once when sufficient input remains and the
   candidate bytes contain no `0xff`; fall back to the current byte reader for
   stuffing, markers and short tails.
2. Keep the existing 8-bit table and add compact 9-to-12-bit subtables only
   for 8-bit prefixes that miss. Codes longer than the subtable still use the
   canonical loop.

A flat 12-bit table per Huffman table is too expensive for this memory target.
Sparse second-level tables preserve most of the possible VLC gain without the
large permanent allocation.

### 4. A/B table-driven chroma contributions

For every 2x2 pixel group, the RGB565 path computes four chroma coefficient
products, performs twelve clipping-table byte reads, and issues four 16-bit
stores. The clipping table should remain; it is compact and branch-free.

Test four 256-entry `int16_t` contribution tables:

```text
red_from_v[v]
blue_from_u[u]
green_from_u[u]
green_from_v[v]
```

They consume 2048 bytes and replace the chroma multiplies and synthesized
constant-multiply sequences with four indexed loads and one green addition.
Also test packing each adjacent RGB565 pair into one aligned 32-bit store.
Keep these changes separately switchable: added DRAM/cache traffic may offset
the saved ALU instructions on ESP32.

### 5. Split the IDCT object to reclaim IRAM

This is primarily a memory improvement that enables further speed work.

All twelve IDCT functions share one `.iram1` input section. The decoder's
dispatch setup references them, so the linker retains 6896 bytes of IDCT code,
although the fixed Player hot path executes only the first 0x230-byte
non-rotated 8x8 function. The remaining rotation and scale functions occupy
about 6.2 KiB of IRAM and cannot be garbage-collected independently. The
object also contributes the shared 1024-byte clipping table.

Recreate the object with one section per function. Another A/B option is to
move the complete original IDCT section to flash, rename its 8x8 symbol to an
`_original` fallback, and provide a standalone first-party
`idct_block_8_8` in IRAM. Reclaimed IRAM can hold the specialized MCU loop or
entropy subtables. This change alone should not be counted as a decode-speed
gain.

## How to modify the prebuilt backend safely

Do not hex-patch instructions in `libesp_new_jpeg.a`. Preserve the pinned
archive as the reference backend and build small first-party replacement
objects.

For an experiment:

1. Rename the selected original symbol to an `_original` name in a generated
   copy of the archive with the pinned Xtensa `objcopy`.
2. Link a replacement object exporting the original ABI name.
3. Keep a build option that selects the untouched archive.
4. Inspect the final ELF to verify calls, sections and literal placement.

This follows the component license, keeps changes reproducible and makes every
optimization independently reversible.

## Acceptance protocol

Each candidate should be a separate A/B change.

1. Run QEMU over complete representative MJPEG files and require identical
   frame count, decoded RGB565 hash, output sizes and error status.
2. Flash the same baseline and candidate builds to the physical ESP32.
3. Warm the decoder, alternate baseline/candidate order, and compare medians
   plus paired samples over multiple runs.
4. Record entropy, IDCT, color and whole-frame times separately where possible.
5. Keep a speed change only when its improvement is larger than run-to-run
   noise and it does not regress memory limits or bit-exact output.

DSP substitutions are lower priority. The hot IDCT already uses `mull`,
shift/add constant products, narrow loads/stores and an assembly loop.
`MAC16` variants would require different fixed-point staging and carry a high
bit-exactness risk. They should be considered only after the row shortcut,
specialized MCU path and entropy experiments are measured.
