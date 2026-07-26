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

The linked Player map currently reports:

| Hot section | Linked size |
| --- | ---: |
| `jpeg_dec_huffman` | 0x520 bytes |
| `jpeg_dec_proc_yuv420_0_block` | 0x5ea bytes |
| `jpeg_dec_process_0` | 0x150 bytes |
| `yuv420_to_rgb565le` | 0x1de bytes |
| Complete IDCT object | 0x1af0 bytes |

## Recommended experiments

### 1. Add an IDCT row/DC-only shortcut

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

This can remain ABI-compatible by replacing only `idct_block_8_8`; rotation and
scaled variants can continue using the original implementation.

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
