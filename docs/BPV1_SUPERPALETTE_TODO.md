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

- [ ] Extract every 64x16 active bank and colour-use histogram per GOP.
- [ ] Count exact 48-byte palette reuse across GOPs.
- [ ] Build deterministic 128-, 256- and 512-palette file-level banks.
- [ ] Map each GOP palette to its nearest file-level palette.
- [ ] Report file overhead, literal fallbacks and weighted RGB error.
- [ ] Add automated tests for deterministic output and accounting.

### 2. Test compression-quality trade-offs

- [ ] Measure exact-only mapping, which must be pixel-identical.
- [ ] Measure bounded approximate mappings at several RGB error limits.
- [ ] Compare complete-file savings rather than palette-section savings.
- [ ] Reject approximate mappings that spend image quality for less than a
      1% complete-file reduction.

### 3. Test interaction with block coding

- [ ] Re-encode at least one short representative sequence using 64 palettes
      selected from each candidate superbank.
- [ ] Compare bytes, RGB PSNR and all five block-mode counts with the current
      per-GOP 64-palette trainer.
- [ ] Check whether stable global palette identities increase exact temporal
      or dictionary matches.

### 4. Decide the format

- [ ] If the acceptance gate passes, specify a backward-compatible BPV
      version with keyframe-only mappings and literal fallback.
- [ ] If it fails, record the measurements here and mark the experiment as
      rejected without changing the production bitstream.

### 5. Hardware validation, only after the format gate passes

- [ ] Materialize selected palettes into the existing 64x16 RGB888/RGB565
      active table before block decoding.
- [ ] Verify unchanged per-block rendering and decoded-frame hashes.
- [ ] Measure decoder memory, average cycles and tail latency on the ESP32.

## Status

The baseline and acceptance gate are established. No production BPV syntax
has been changed yet.
