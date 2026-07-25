# BPV1 superpalette experiment TODO

## Goal

Test whether a file-level bank containing more than 64 palettes can improve
BPV compression without increasing the ESP32 block-decoding cost.

The proposed representation is:

- train a file-level bank of 128, 256 or 512 palettes;
- keep 64 stable active palette slots inside each GOP;
- select the active slots from the file-level bank at a keyframe;
- do not change slot assignments inside a GOP, so `SKIP`, `MOTION` and
  dictionary records retain their meaning;
- materialize the selected 64x16 colours into the existing active palette
  table before decoding the keyframe blocks;
- allow a literal 48-byte palette as a fallback when no file-level palette
  satisfies the quality threshold.

An arbitrary mapping per frame is intentionally excluded. Reassigning an
active slot changes the meaning of previous-frame and dictionary records.
Avoiding that would require reference translation, selective invalidation or
another retained palette state in the decoder.

## Acceptance criteria

Keep a production bitstream change only when all of these conditions hold:

- complete-file size falls by at least 1% at the same RGB PSNR, or RGB PSNR
  improves at the same complete-file size;
- the decoded block image remains deterministic and all v1-v4 compatibility
  tests continue to pass;
- the per-block decoder and renderer perform no additional palette lookup;
- the ESP32 decoder does not retain the complete file-level palette bank;
- active-slot changes happen only where temporal references and dictionaries
  are reset;
- the physical-board decode-time and RAM measurements do not regress.

The 1% gate applies to the complete file, not only to the palette section.

## Baseline

The retained 320x240 v4 test stream is
`out/VID_20260522_181611_v4.bpv1`:

- 3,359 frames at 30 fps;
- GOP 48, therefore 70 active-palette updates;
- 145,392,123 total bytes;
- 70 x 3,072 = 215,040 active-palette bytes;
- active palettes occupy about 0.148% of the complete file;
- RGB PSNR reported by the encoder: 31.545129790 dB.

A 256-palette file-level RGB888 bank would require 12,288 bytes. Seventy
64-entry mappings with one-byte IDs would require another 4,480 bytes. The
maximum direct saving, before literal fallbacks and format fields, is only:

```text
215,040 - 12,288 - 4,480 = 198,272 bytes
198,272 / 145,392,123 = 0.136%
```

Consequently, the experiment can pass the 1% gate only if the larger palette
candidate pool also improves block decisions (`SKIP`, `MOTION`, block
dictionary, pattern dictionary or smaller RAW payloads).

## Tasks

### 1. Measure palette reuse without changing the bitstream

- [x] Extract every 64x16 active bank and colour-use histogram per GOP.
- [x] Count exact 48-byte palette reuse across GOPs.
- [x] Build deterministic 128-, 256- and 512-palette file-level banks.
- [x] Map each GOP palette to its nearest file-level palette.
- [x] Report file overhead, literal fallbacks and weighted RGB error.
- [x] Add automated tests for deterministic output and accounting.

### 2. Test compression-quality trade-offs

- [x] Measure exact-only mapping, which must be pixel-identical.
- [x] Measure bounded approximate mappings at several RGB error limits.
- [x] Compare complete-file savings rather than palette-section savings.
- [x] Reject approximate mappings that spend image quality for less than a
      1% complete-file reduction.

### 3. Test interaction with block coding

- [x] Re-encode at least one short representative sequence using 64 palettes
      selected from each candidate superbank.
- [x] Compare bytes, RGB PSNR and all five block-mode counts with the current
      per-GOP 64-palette trainer.
- [x] Check whether stable global palette identities increase exact temporal
      or dictionary matches.

### 4. Decide the format

- [x] Confirm that the acceptance gate did not pass, so no new BPV version is
      specified.
- [x] Record the measurements here and mark the experiment as
      rejected without changing the production bitstream.

### 5. Hardware validation, only after the format gate passes

- [x] Do not implement or benchmark the decoder path because the format gate
      failed before hardware validation.

## Full-file palette analysis

`codecs/bpv/tools/bpv1superpalette.js` scanned all 16.1 million decoded block
records of the retained 320x240 stream. The result was:

- all 4,480 active palette rows were used by at least one decoded pixel;
- all 4,480 rows were byte-wise unique;
- exact-only mapping made the file larger by 688, 816 and 1,162 bytes for
  banks of 128, 256 and 512 palettes respectively;
- allowing an additional 40 dB perturbation relative to the retained BPV
  reconstruction saved only 44,820, 49,897 and 55,127 bytes;
- the best complete-file saving was therefore 0.0379%, far below the 1% gate.

The exact-only result is larger because every bank entry merely replaces one
literal palette row, while the mapping bitmap and IDs remain as overhead.

## Block-coding A/B test

The source was the first 480 frames of the required repository source:

`out/sources/big_buck_bunny_1080p_h264/big_buck_bunny_1080p_h264.mov`

All variants used 320x180, native 24 fps, GOP 48, three candidate palettes,
motion radius 2 and 256-entry block/pattern dictionaries. The baseline used
the current per-GOP trainer. The experimental encoder consumed 64 mapped
palette rows per GOP while writing an ordinary BPV1 v4 stream.

Matched-quality results:

| Palette training | Lambda | Bytes | RGB PSNR | Size vs baseline |
| --- | ---: | ---: | ---: | ---: |
| Current per-GOP 64 | 64 | 3,317,843 | 31.261883 dB | baseline |
| Superbank 128 | 16 | 4,378,895 | 31.271768 dB | +31.98% |
| Superbank 256 | 48 | 3,545,075 | 31.264993 dB | +6.85% |
| Superbank 512 | 60 | 3,370,165 | 31.258813 dB | +1.58% |

At the unchanged lambda 64, stable shared palettes did produce more exact
reuse in the 128- and 256-palette cases, but only by accepting lower quality:

| Superbank | Bytes vs baseline | PSNR change | SKIP change |
| --- | ---: | ---: | ---: |
| 128 | -2.13% | -1.035 dB | +17,407 |
| 256 | -0.67% | -0.385 dB | +9,064 |
| 512 | -0.16% | -0.113 dB | +2,181 |

Restoring baseline quality required a lower lambda and more RAW data, making
every superbank variant larger. All three resulting streams passed complete
`bpv1info` validation. The native encoder/decoder compatibility suite and the
JavaScript test suite also pass.

## Decision

**Rejected for the production BPV format.**

The global palette identities improve exact temporal reuse, but not enough to
pay for their loss of GOP-specific colour accuracy. The same-quality result
is 1.58% to 31.98% larger, and palette-section-only compression cannot reach
the 1% complete-file gate even with visible additional error.

No new BPV version or ESP32 decoder state is justified. The analysis tool and
the encoder's active-palette override remain useful for future palette
experiments without changing the production bitstream.
