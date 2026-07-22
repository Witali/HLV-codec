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
- [ ] Compile decoder statistics out of the ESP32 hot path.
- [ ] Add fixed Y6/U5/V5 unpack kernels.
- [ ] Add an ESP32-oriented 32-bit bitreader.
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

The ESP-IDF size report after the SKIP change remains at 49,328 bytes of static
DRAM. The larger decoder code increases Flash Code from 149,120 to 155,296 bytes
but does not consume additional runtime RAM.

Host timings are comparative, not estimates of ESP32 wall-clock performance.
Optimisations that help x64 but add work to 32-bit Xtensa must be validated on
the board before being considered final.
