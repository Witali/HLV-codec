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

- [ ] Profile coefficient parsing and IDCT separately with cycle counters so
      the next transform experiment targets measured hot cases.
- [ ] Add small, generated fast tables for the most frequent DCT coefficient
      VLC prefixes, retaining the existing tree as the fallback.
- [ ] Specialize motion compensation by the four half-pixel modes at the
      16x16/8x8 macroblock level, avoiding a per-pixel mode branch while
      retaining the current full-row path.
- [ ] Evaluate an IDCT DC-only fast path if coefficient telemetry shows enough
      eligible blocks. The previous generic sparse row/column checks were too
      expensive.
- [ ] Reduce display-side packed-sample unpack work while preserving the
      Y6/U5/V5 correction table bit for bit.

## Test commands

```powershell
.\scripts\test_mpeg1_compact.ps1 `
    -InputFile .\out\VID_20260522_181611_center-crop_240x180_mpeg1_q3_native-fps.mpg

.\firmware\esp32_2432s028_hlv_player_idf_c\flash.ps1 -Port COM8

.\firmware\esp32_2432s028_hlv_player_idf_c\capture-player-metrics.ps1 `
    -Port COM8 -Frames 300 -TimeoutSeconds 60 -AllowAudioUnderrun
```
