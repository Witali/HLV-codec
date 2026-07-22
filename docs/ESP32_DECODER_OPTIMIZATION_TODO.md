# ESP32 decoder performance TODO

All changes in this list must keep ESP32 heap, static DRAM and decoder stack
usage at or below the current build. Each accepted step is tested with the
host-side compact-decoder simulator and must preserve the complete reconstructed
frame hash. Board timings will be added when the CYD2USB board is available.

Test stream: `out/video.hlv`, HLV v12, 320x180, 8,947 frames. The simulator
uses packed Y6/U5/V5 reference frames and segmented 8 x 7,680-byte packet views,
matching the firmware's decoder-facing storage model.

## Checklist

- [x] Establish reproducible simulator baseline.
- [x] Remove all SKIP unpacking by reading compact intra neighbours from the
      packed current frame.
- [x] Compile decoder statistics out of the ESP32 hot path.
- [x] Evaluate fixed Y6/U5/V5 unpack kernels (rejected: slower).
- [x] Add an ESP32-oriented 32-bit bitreader.
- [ ] Specialise integer, half-pixel and quarter-pixel interpolation.
- [ ] Copy eligible zero-residual INTER/GLOBAL blocks in packed form.
- [ ] Optimise sparse residual and WHT paths without new lookup buffers.
- [ ] Compare compiler/code-layout variants and retain the fastest no-RAM option.
- [ ] Remove repeated address calculations from macroblock hot paths.
- [ ] Rebuild ESP-IDF and compare DRAM, heap allocations and binary size.

## Results

| Step | Simulator time | FPS | Change | Frame hash | RAM change | Decision |
|---|---:|---:|---:|---|---:|---|
| Baseline after packed SKIP copy | 15.493 s | 1732.5 | — | `bdb0842a1e1a3a72` | 0 | accepted |
| No compact SKIP unpack | 14.155 s | 1896.2 | +9.5% FPS | `bdb0842a1e1a3a72` | 0 | accepted |
| Statistics compiled out | 13.544 s | 1981.7 | +4.5% FPS | `bdb0842a1e1a3a72` | -280 B heap | accepted |
| Fixed Y6/U5/V5 unpack kernels | 14.016 s | 1915.0 | -3.4% FPS | `bdb0842a1e1a3a72` | 0 | rejected |
| 32-bit bitreader (x86 simulator) | 15.574 s | 1723.5 | +11.8% vs BR64 x86 | `bdb0842a1e1a3a72` | reduced stack | accepted |

The ESP-IDF size report after the SKIP change remains at 49,328 bytes of static
DRAM. The larger decoder code increases Flash Code from 149,120 to 155,296 bytes
but does not consume additional runtime RAM.

Compiling decoder statistics out also removes the 35 64-bit counters from each
decoder instance (280 bytes). Static DRAM remains 49,328 bytes, while Flash Code
falls from 155,296 to 147,936 bytes. `hlv1_decoder_stats()` remains ABI-compatible
and returns a zero-filled read-only record in this build.

The bitreader comparison uses the simulator's 32-bit x86 target so the cost of
64-bit shifts is represented: BR64 took 17.414 s (1541.4 FPS), while BR32 took
15.574 s (1723.5 FPS). On x64 BR32 is 3.2% slower, as expected from its smaller
refill reservoir. ESP-IDF static DRAM remains 49,328 bytes; final Xtensa timing
still needs the physical board.

## Simulation layers

- The native decoder simulator is the reproducible performance and bit-exactness
  test used by this checklist.
- [Espressif QEMU](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/qemu.html)
  can boot and debug the complete ESP-IDF image and is suitable
  for panic, allocation, task and container-I/O smoke tests. It does not model
  the CYD board's ST7789/SD wiring or provide cycle-accurate performance.
- [Wokwi](https://docs.wokwi.com/guides/esp32) models ESP32, SPI and DMA and can
  load ESP-IDF `flasher_args.json`, so it
  is useful for peripheral integration. Its simulated CPU frequency is normally
  capped, therefore wall-clock playback speed is not a board benchmark.

Host timings are comparative, not estimates of ESP32 wall-clock performance.
Optimisations that help x64 but add work to 32-bit Xtensa must be validated on
the board before being considered final.
