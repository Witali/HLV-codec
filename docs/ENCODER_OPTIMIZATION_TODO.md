# Encoder optimization TODO

This checklist tracks host-encoder optimizations that must preserve encoded
quality.  Unless a format experiment explicitly says otherwise, an accepted
change must produce byte-identical HLV and reconstructed Y4M output.

## Reference workload

- Source:
  `out/sources/big_buck_bunny_1080p_h264/big_buck_bunny_1080p_h264.mov`
- Input conversion: 320x180 YUV420, 24 fps, frames 2880 through 3239.
- Encoder: balanced preset, syntax v13, quality 45, GOP 30.
- Timing: median of at least three runs after one warm-up run.
- Correctness: compare SHA-256 of both HLV and reconstructed Y4M against the
  scalar single-thread reference.
- Thread coverage: `--threads 1`, `--threads 4`, `--simd off`, and
  `--simd auto`.

Timing is machine-specific, so every result also records architecture-neutral
encoder work counters:

- motion-search SAD evaluations and luma samples visited;
- global-motion SAD evaluations and luma samples visited;
- RDO squared-error samples;
- forward and inverse 4x4 WHT blocks;
- quantized coefficients;
- palette distance evaluations;
- candidate-slot initializations and complete residual candidates;
- bit-writer calls, appended bits, and byte-copyable appended bytes.

The counters describe algorithmic work, not exact processor instructions.
They remain comparable across scalar, SSE2, AVX2, compiler, and host CPU
implementations.  A separate weighted operation estimate may be reported, but
the raw counters are the acceptance evidence.

Run the standard comparison with:

```powershell
.\scripts\python.ps1 .\scripts\benchmark_encoder.py `
    --json .\out\benchmarks\encoder.json
```

The stable primitive-operation estimate uses these weights per sample or
block: integer/H-V/2-D SAD `3/7/11`, copied/H-V/2-D prediction `1/5/9`,
RDO squared error `3`, forward/inverse 4x4 WHT `64/80`, quantization `5`,
palette distance `10`, and one operation per requested or appended bit.  The
estimate is useful for comparing algorithms; raw counts and wall time remain
authoritative.

## Work list

- [x] Add encoder work counters and a repeatable benchmark report.
- [x] Record the instrumented SIMD baseline.
- [ ] Remove duplicate motion SAD calculations.
  - return the winning SAD from single-candidate searches;
  - do not recompute zero/global/rounded motion vectors already in the list;
  - do not evaluate the coarse global `(0, 0)` position twice.
- [ ] Add exact SAD early termination against the current top-N threshold.
- [ ] Reuse four 8x8 coarse SAD maps to derive 16x16 and rectangular costs.
- [ ] Add exact zero-residual encoder reconstruction without inverse WHT.
- [ ] Add exact DC-only encoder reconstruction without the general inverse
  WHT.
- [ ] Add an exact palette-impossibility prefilter before 2/4/8-color
  clustering.
- [ ] Reuse candidate and residual bit-writer storage.
- [ ] Add byte-aligned and shifted bulk paths to `hlv1_bw_append`.
- [ ] Count residual representation lengths first and emit only the selected
  v9 representation.
- [ ] Use SKIP as an early RDO upper bound while retaining the original
  candidate tie priority.
- [ ] Add monotonic candidate cutoffs using partial distortion and bit cost.
- [ ] Replace GOP batches with a persistent ordered worker queue.
- [ ] Evaluate strict-FP MSVC `/GL` + `/LTCG` and profile-guided optimization.
- [ ] Evaluate batched AVX2 SAD/WHT with runtime dispatch and scalar/SSE2
  fallbacks.

## Rejected approaches

- Reducing motion search radius or the number of RDO candidates.
- Approximate quantization or unchecked reciprocal division.
- `/fp:fast`, `-ffast-math`, or changes to floating-point RDO ordering.
- Reordering candidates without preserving the existing strict-`<` tie rule.
- SIMD kernels retained only because they are vectorized: every kernel must
  improve the measured workload.  The earlier fractional predictor SSE2
  experiment is the precedent; it was removed after a timing regression.

## Results

Results are added here one focused change at a time.  Retained changes include
the output hashes, raw operation counts, median time, and relative throughput.
Rejected changes remain documented with the reason for rejection.

### Instrumented baseline

Commit `dd7d2fb`, 360 frames, three measured runs after one warm-up:

| Pipeline | Threads | Median | Throughput | Primitive ops/frame |
|---|---:|---:|---:|---:|
| Scalar | 1 | 23.995 s | 15.00 fps | 158,017,199 |
| SSE2 | 1 | 18.365 s | 19.60 fps | 158,017,199 |
| Scalar | 4 | 6.921 s | 52.02 fps | 158,017,199 |
| SSE2 | 4 | 5.526 s | 65.15 fps | 158,017,199 |

All variants produced identical output:

- HLV SHA-256:
  `4d36a41966d3c165ee4b4d0de58575cccb9f68a2b9ba8682fd45f2c88150e05d`
- reconstructed Y4M SHA-256:
  `ded2a57caca791ecce5342dccc0991cecf35366464c92c71f6aa59e93285e321`

Selected raw totals for the 360 frames:

| Counter | Total |
|---|---:|
| Motion/global SAD evaluations | 35,476,479 / 31,395 |
| Integer/H-V/2-D SAD samples | 3,362,114,355 / 163,839,940 / 153,628,484 |
| Copied/H-V/2-D prediction samples | 160,658,400 / 141,911,616 / 41,354,592 |
| RDO squared-error samples | 973,021,824 |
| Forward/inverse WHT blocks | 29,962,488 / 29,962,488 |
| Quantized coefficients | 479,399,808 |
| Palette distance evaluations | 2,952,806,400 |
| Candidate initializations/residual candidates | 1,742,679 / 2,355,659 |
| Bit-writer put calls/buffer grows | 683,126,180 / 5,516,655 |
