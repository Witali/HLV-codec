# MPEG-1 decoder optimization plan

This file records physical ESP32 results for the constrained standard MPEG-1
Program Stream profile. The test clip is
`out/VID_20260522_181611_center-crop_240x180_mpeg1_q3_native-fps.mpg`:
240x180, 30 fps, 3,359 frames, no B pictures, MP2 mono at 32 kHz. Every
accepted decoder change must preserve the complete compact decode checksum
`6bc59309b0d7dc23`.

## Format constraint

- Keep MPEG files standard MPEG Program Streams.
- Do not add project-specific MPEG wrappers, appended PCM tracks or private
  container extensions.
- The standard audio profile remains MP2. PCM_U8 is valid only in the
  project's existing HLV, BPV and AVI profiles.

## Results

| Experiment | ESP32 result | Decision |
| --- | --- | --- |
| 40 MHz SD baseline | decode 57,380.6 us; 17.301 fps | baseline |
| 32-bit cached bitreader and local VLC lookahead | decode 52,090.8 / 52,096.5 us; 18.634 / 18.688 fps | accepted, `8f82319` |
| Sparse IDCT row and column skipping | decode 52,558.1 us | rejected, 0.9% slower |
| Sparse IDCT column skipping only | decode 52,195.0 us | rejected, 0.2% slower |
| Packed copy of unchanged zero-motion 8x8 blocks plus selective unpack | decode 50,828.8 / 50,832.8 us; 18.965 / 19.002 fps | accepted, `dd77067` |
| Direct packed motion prediction without residual | decode 50,979.8 / 50,934.3 us | rejected, 0.25% slower |
| Prediction plus residual in one 8x8 scratch block | decode 52,864.5 us | rejected, 4.0% slower |
| Scratch block with a direct integer-motion path | decode 53,785.4 us | rejected, 5.8% slower |
| One shared demux and two 4 KiB ES buffers | 18.524 fps; MP2 26,870 / 26,419 us; 196-200 ms video peaks | rejected |
| YUV-to-RGB565 lookup tables | render 27,770.6 / 27,430.3 us; 19.194 / 19.213 fps | accepted, `bc2fa66` |
| Non-standard PCM_U8 MPEG wrapper | conflicts with the standard-format rule | not applicable |
| Cache-safe 23-bit slice-ending peek | q41 decode 67.936 -> 68.041 ms (+0.154%); QEMU 2,715,338 -> 2,711,931 cycles (-0.125%); checksums unchanged | rejected; hardware regressed |
| Build-time MPEG-1 phase profiler | q41 profile repeats within 0.014%; final OFF build 67.936 ms versus 67.936 ms baseline; identical image size | accepted diagnostic; default OFF |
| Explicit compact MC half-pixel mode loops | heavy q41 QEMU 2,715,338 -> 2,855,545 cycles (+5.16%); identical hash | rejected before flash; implementation removed |
| Fused IDCT row reconstruction | heavy q41 QEMU 2,715,338 -> 2,754,921 cycles (+1.46%); identical hash | rejected before flash; implementation removed |
| Selective IRAM for compact MC | q41 67.935 -> 62.656 ms (-7.77%); Regression 59.999 -> 55.311 ms (-7.81%); checksums unchanged | accepted; +10,352 bytes IRAM |

The accepted decoder changes reduce average video decode time by 11.4%,
from 57,380.6 to about 50,831 us. The render lookup tables then remove roughly
86,000 to 120,000 integer multiplications per frame, depending on whether the
240x180 picture is rendered at native size or scaled to 320x240.

### IDF C renderer phase profile — 2026-08-02

`MPEG1_RENDER_PROFILE=1` measured the first 60 frames of
`BigBuckBunny_320x180_24fps_MPEG1_41dB.mpg` on the physical
ESP32-D0WD-V3 revision 3.1 at 240 MHz. The figures below are independent
per-phase medians from three runs. The complete renderer median was 27.462 ms,
within 0.19% of the preceding uninstrumented 27.515 ms result.

| Render phase | Median per frame | Share |
| --- | ---: | ---: |
| Wait for a reusable LCD DMA strip | 0.017 ms | 0.1% |
| Unpack compact Y6 | 8.489 ms | 30.9% |
| Unpack compact U5/V5 | 4.350 ms | 15.8% |
| Build cached chroma contribution rows | 1.380 ms | 5.0% |
| LUT, clamp and pack RGB565 pixels | 11.224 ms | 40.9% |
| Queue 12 LCD strip transactions | 1.829 ms | 6.7% |
| Row-loop and control remainder | 0.173 ms | 0.6% |

The full 320x180 RGB565 payload needs 11.520 ms of wire time at the configured
80 MHz LCD SPI clock, but this runs concurrently with CPU conversion. The
measured DMA-strip wait is only 0.017 ms, showing that unpacking and RGB565
preparation, not LCD bandwidth, determine the renderer wall time. The final
queued transactions can also finish after `renderMpegFrame()` returns and are
therefore not charged to `render_us`.

### Fused compact YUV-to-RGB565 renderer — 2026-08-02

The IDF C native renderer now handles aligned Y6/U5/V5 input in 16x2 luma
tiles. Each tile unpacks byte-aligned compact spans, applies the existing Q4
correction pattern, reuses eight U/V samples for both YUV420 rows, performs
branch-free clamp-and-pack lookup, and stores pairs of RGB565 pixels directly
in the LCD DMA strip. Scaled and unaligned pictures retain the reference row
renderer as a fallback.

The final build keeps the fused 3,024-byte kernel in IRAM. Independent medians
from three 60-frame physical ESP32 runs were:

| Metric | Reference | Fused renderer | Change |
| --- | ---: | ---: | ---: |
| Complete renderer | 27.462 ms | 25.706 ms | -6.4% |
| Compact unpack + chroma + RGB565 | 25.443 ms | 23.284 ms | -8.5% |
| Static DRAM | 50,828 bytes | 54,796 bytes | +3,968 bytes |
| IRAM | 104,603 bytes | 107,627 bytes | +3,024 bytes |

An A/B build with only the general clamp tables was 0.31 ms slower, so those
tables are restricted to the fused tile kernel and the reference
`yuvToRgb565()` path remains unchanged. Keeping the fused kernel in flash was
about 0.22 ms slower than IRAM. The verification build rendered both paths and
compared every output row: all 5,400 row pairs from 60 complete 320x180 frames
matched bit for bit. The test restored the original 43-byte `play.txt` with
CRC32 `ebbae2ae`; the final 52-entry SD listing contained no test-only files.

### Compact Q4 correction LUT — 2026-08-02

The fused renderer now shares one `int8_t[4][23][4]` correction LUT between
the Y, U and V planes. Its 368-byte size is enforced by a C99 static assertion
and confirmed as `0x170` bytes in the ESP32 linker map. Each lookup selects
four ordered-dither corrections using the row phase and the encoder's bounded
Q4 value. An exact calculated fallback remains available for Q4 values outside
`-8..14`. `COMPACT_YUV_Q4_LUT` controls the optimization at build time and
defaults to `ON`.

Independent medians from three 60-frame physical runs, with every other build
option held equal, were:

| Metric | Q4 calculation | 368-byte LUT | Change |
| --- | ---: | ---: | ---: |
| Complete renderer | 25.813 ms | 25.640 ms | -0.173 ms (-0.67%) |
| Fused compact-to-RGB565 phase | 23.726 ms | 23.366 ms | -0.361 ms (-1.52%) |
| Static DRAM | baseline | baseline + 368 bytes | +368 bytes |

The verification build compared the optimized path with the reference
renderer on the same board. All 5,400 row pairs from 60 complete 320x180
frames matched bit for bit (`CRV,60,5400,5400`). Both A/B runs had zero frame
gaps, display skips, audio rebuffer events and audio underruns. The original
43-byte `play.txt` was restored byte for byte (CRC32 `ebbae2ae` and matching
SHA-256), and a fresh listing contained the same 52 persistent SD files.

## Completed work

- [x] Restore the SD bus to 40 MHz after the reliability test.
- [x] Add a 32-bit cached bitreader.
- [x] Walk VLC tables from one local lookahead value.
- [x] Test sparse IDCT row/column pruning.
- [x] Copy unchanged zero-motion blocks directly between packed reference
      frames.
- [x] Unpack only coded zero-motion blocks before residual reconstruction.
- [x] Test direct packed motion prediction without a residual.
- [x] Test per-block prediction, IDCT, packing and correction accumulation.
- [x] Test removal of the 7,680-byte full-width macroblock-row work area.
      Removal depends on the slower per-block scratch path and is therefore
      rejected for the speed-oriented build.
- [x] Test one shared MPEG demux for video and audio.
- [x] Replace repeated YUV conversion multiplications with lookup tables.
- [x] Evaluate PCM_U8 audio and reject it for MPEG because it would require a
      non-standard container or codec combination.

## Next candidates

- [x] Test a cache-safe 23-bit non-zero lookahead. Three 300-frame q41 runs
      regressed by 0.154% on the physical ESP32 despite a 0.125% QEMU cycle
      improvement, so the implementation and build option were removed.
- [x] Add a build-time `PLM_MPEG_DECODE_PROFILE` option, defaulting to `OFF`, and
      measure coefficient/VLC parsing, IDCT, motion compensation and packed
      reconstruction independently. Two physical q41 profiles repeated within
      0.014%; the corrected OFF build has no measurable release overhead.
- [x] Specialize motion compensation by the four half-pixel modes at the
      16x16/8x8 macroblock level. Explicit copy loops increased heavy q41 QEMU
      cycles by 5.16%, indicating that the O3 loop/code layout was already
      preferable. The implementation and build option were removed without
      flashing the rejected binary.
- [x] Fuse the IDCT row pass with destination put/add/clamp. Both an indexed
      temporary and direct scalar stores preserved the heavy q41 hash but were
      1.46% slower in QEMU. The implementation and build option were removed
      without flashing the rejected binary.
- [x] Test selective IRAM placement only for the measured motion-compensation
      hot kernels. The two compact kernels use 10,352 bytes and leave 12,704
      bytes to the SRAM1 boundary. Physical q41 and Regression medians improved
      by 7.77% and 7.81%, so the option is retained and defaults to `ON`.
- [ ] Add a compact generated second-level table for only the unresolved
      six-bit DCT coefficient prefixes, retaining the existing VLC tree as the
      fallback. Do not replace it with a large generic FFmpeg-style table
      unless a separate flash/cache A/B supports that choice.
- [ ] Compile out remaining B-picture/backward-prediction decisions in the
      constrained no-B profile and replace repeated macroblock address
      division/modulo only if profiling shows a measurable benefit.
- [x] Keep the existing DC-only IDCT fast path. `plm_video_decode_block()`
      already handles `n == 1`; the previous TODO item was stale.
- [x] Reduce display-side packed-sample unpack work while preserving the
      Y6/U5/V5 correction table bit for bit.

### IDF C decoder phase profile — 2026-08-04

`PLM_MPEG_DECODE_PROFILE=ON` measured the first 60 frames of the available
q41 heavy clip on the physical ESP32 at 240 MHz. Instrumentation is deliberately
excluded from normal builds. Cycle counters are nested, so each named phase is
exclusive while `total` also includes parser and macroblock control overhead.

| Decode phase | Cycles, 60 frames | Per frame | Share |
| --- | ---: | ---: | ---: |
| Coefficient and VLC parsing | 374,407,065 | 26.000 ms | 34.9% |
| Motion compensation | 366,662,776 | 25.463 ms | 34.2% |
| Residual reconstruction and IDCT | 170,956,289 | 11.872 ms | 16.0% |
| Compact Y6/U5/V5 reconstruction | 122,700,700 | 8.521 ms | 11.5% |
| Decoder control remainder | 36,957,873 | 2.566 ms | 3.5% |
| Total | 1,071,684,703 | 74.423 ms | 100% |

The repeat total was 1,071,829,907 cycles, a 0.014% difference. Both runs
decoded 78,433 blocks with identical classification: 5,320 DC-only blocks,
73,113 general-IDCT blocks and 15,505 motion-compensated macroblocks. The
instrumented total must not be compared directly with release timing because
the cycle reads perturb short operations.

The final default-OFF release produced the same `0xbcc50` application image
size as the baseline. Its three-run q41 median was 67.9365 ms versus 67.9359 ms
baseline (+0.0009%), with zero frame gaps or display skips. An earlier design
that left unused profiler entry points in the OFF binary measured 68.2489 ms
(+0.46%); that layout was discarded completely.

### Selective compact motion IRAM — 2026-08-04

The phase profile identified motion compensation as 34.2% of instrumented
q41 decode time. `PLM_MPEG_IRAM_COMPACT_MC=ON` moves only GCC's luma and
chroma `plm_video_process_compact_macroblock()` constprop kernels from mapped
flash to IRAM. Their linked code plus alignment is 10,352 bytes. Normal heap
start is unchanged, while `_iram_end` moves from `0x4009a5f0` to `0x4009ce60`,
leaving 12,704 bytes before `0x400a0000`.

Independent medians from three physical runs per A/B variant were:

| Clip | Flash kernels | IRAM kernels | Change |
| --- | ---: | ---: | ---: |
| Danila 320x240 q41, 300 frames | 67.9350 ms | 62.6558 ms | -5.2792 ms (-7.77%) |
| VideoFormatRegression, 60 frames | 59.9989 ms | 55.3108 ms | -4.6881 ms (-7.81%) |

Every run had the requested frame count, zero sequence gaps and zero display
skips. The QEMU A/B retained hash `9ee0136f7b1ee63f`; its instruction-count
average was exactly 2,715,338 cycles for both placements, as expected because
QEMU does not model the physical mapped-flash cache penalty. MP2 `A` telemetry
was absent in both physical A and B builds, so this is not an IRAM regression;
video metadata consistently reported the expected 32 kHz audio stream.

## Current heavy-clip campaign

The speed decision for every new candidate is made on the physical
ESP32-D0WD-V3 at 240 MHz with the existing 40 MHz SD configuration. The
primary corpus deliberately uses the slowest retained 320x240 MPEG-1 files:

| Clip | Previous decode average | Purpose |
| --- | ---: | --- |
| `Danila_320x240_30fps_MPEG1_44dB.mpg` | 86.491 ms | historical highest-load case; not present locally or on the current SD card |
| `Danila_320x240_30fps_MPEG1_41dB.mpg` | 71.746 ms | primary available production quality point |
| `VideoFormatRegression_320x240_30fps_MPEG1_q3.mpg` | 57.874 ms | independent content/regression case |

`BigBuckBunny_320x240_24fps_MPEG1_40dB.mpg` is not a speed acceptance clip:
its previous 16.117 ms decode time is too light to expose small hot-path
changes. It remains a guard against a broad performance or playback
regression.

For each candidate:

1. Run the complete host compact decoder and preserve the expected frame
   count and checksum. Exercise a compressed frame larger than the refill
   buffer and compare it with contiguous input where the test supports both.
2. Run the IDF C MPEG-1 QEMU benchmark and require identical decoded frame
   checksums between A and B. Build the preserved IDF C++ variant as well.
3. Flash A and B to the physical board and collect at least three comparable
   runs per variant on the heavy clips. Use the median decode time, while also
   checking frame sequence, keyframe recovery, SD CRC, audio underruns and
   playback errors.
4. Retain a change only when the heavy-clip median improves repeatably and no
   correctness, memory or light-clip regression is observed. Otherwise remove
   its implementation and record the rejected result here.
5. Record any test-only SD filenames before upload. Delete exactly those files
   after the run, obtain a fresh directory listing, and preserve `play.txt`,
   `crc32.txt` and all pre-existing assets.

The hardware gate is mandatory: host or QEMU timing can reject an obviously
bad candidate but cannot accept one. At the start of this campaign on
2026-08-04 Windows reported no serial ports. The board was subsequently
connected as COM8 and the physical A/B gate resumed. The q44 historical clip
is not copied to the card; the current physical corpus is q41 plus Regression.

## Test commands

```powershell
.\scripts\test_mpeg1_compact.ps1 `
    -InputFile .\out\VID_20260522_181611_center-crop_240x180_mpeg1_q3_native-fps.mpg

.\firmware\esp32_2432s028_hlv_player_idf_c\flash.ps1 -Port COM8

.\firmware\esp32_2432s028_hlv_player_idf_c\capture-player-metrics.ps1 `
    -Port COM8 -Frames 300 -TimeoutSeconds 60 -AllowAudioUnderrun
```
