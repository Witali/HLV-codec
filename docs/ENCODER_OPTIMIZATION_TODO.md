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
zero/DC-only reconstruction `16/34`, palette distance `10`, and one operation
per palette-prefilter sample/bin and per requested or appended bit.  The
estimate is useful for comparing algorithms; raw counts and wall time remain
authoritative.

## Work list

- [x] Add encoder work counters and a repeatable benchmark report.
- [x] Record the instrumented SIMD baseline.
- [ ] Remove duplicate motion SAD calculations.
  - [x] return the winning SAD from single-candidate searches;
  - [ ] avoid recomputing zero/global vectors without adding hot-loop
    branches;
  - [x] do not recompute a rounded motion vector already in the retained list;
  - [x] do not evaluate the coarse global `(0, 0)` position twice;
  - [x] do not reevaluate the half-pixel refinement center.
- [ ] Add exact SAD early termination against the current top-N threshold.
- [ ] Reuse four 8x8 coarse SAD maps to derive 16x16 and rectangular costs.
- [x] Add exact zero-residual encoder reconstruction without inverse WHT.
- [x] Add exact DC-only encoder reconstruction without the general inverse
  WHT.
- [x] Add an exact palette-impossibility prefilter before 2/4/8-color
  clustering.
- [x] Reject a clustered palette on the first final sample outside the
  existing Y/UV error limits.
- [ ] Reuse candidate and residual bit-writer storage.
- [x] Add byte-aligned and shifted bulk paths to `hlv1_bw_append`.
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

### Current cumulative result

After commits `84f38d1` through `b89ca41`:

| Metric | Instrumented baseline | Current | Change |
|---|---:|---:|---:|
| Primitive operations/frame | 158,017,199 | 131,787,188 | -16.60% |
| SAD evaluations | 35,476,479 | 34,720,427 | -2.13% |
| General inverse WHT blocks | 29,962,488 | 21,361,972 | -28.70% |
| Bits sent through scalar `put` | 2,470,174,597 | 1,386,949,485 | -43.85% |
| Palette distance evaluations | 2,952,806,400 | 2,247,849,178 | -23.88% |

Final three-run medians under the current host load are:

| Pipeline | Threads | Median | Throughput |
|---|---:|---:|---:|
| SSE2 | 1 | 18.563 s | 19.39 fps |
| SSE2 | 4 | 5.091 s | 70.71 fps |

Host timing varied substantially when the Windows player and other work were
active.  Consequently the deterministic operation counts and the interleaved
per-change A/B results below are the primary optimization evidence.  The final
HLV and reconstructed Y4M SHA-256 values are still the baseline hashes.

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

### Duplicate SAD removal

The retained branch-free subset removes 756,052 SAD evaluations and
79,987,138 integer SAD samples over 360 frames:

| Metric | Baseline | Retained | Change |
|---|---:|---:|---:|
| SAD evaluations | 35,476,479 | 34,720,427 | -2.13% |
| Integer SAD samples | 3,362,114,355 | 3,282,127,217 | -2.38% |
| Primitive operations/frame | 158,017,199 | 157,350,640 | -0.42% |

The HLV and reconstruction hashes remain identical to the baseline.
Interleaved one-thread A/B pairs measured `18.288/18.435` seconds in favor of
the retained version and `22.041/21.759` seconds in favor of the baseline, so
the wall-time effect is below current system noise.  The deterministic
operation reduction is retained.

A broader cache of zero/global SAD values reduced primitive operations by
0.65%, but branches added to every coarse-search candidate made the measured
implementation slower.  That form was rejected; a future shared SAD map can
remove the duplicates without those hot-loop checks.

### Zero and DC-only reconstruction

The encoder now mirrors the decoder's exact zero/DC-only reconstruction paths.
The small helper must be force-inlined: the first ordinary function form
reduced operations but was slightly slower and was rejected.

| Metric | SAD baseline | Zero/DC fast path | Change |
|---|---:|---:|---:|
| General inverse WHT blocks | 29,962,488 | 21,361,972 | -28.70% |
| Zero fast blocks | 0 | 4,591,307 | new |
| DC-only fast blocks | 0 | 4,009,209 | new |
| Primitive operations/frame | 157,350,640 | 156,022,120 | -0.84% |

Two interleaved one-thread A/B pairs averaged 21.411 seconds for the fast path
and 21.938 seconds for its immediate baseline, a 2.4% time reduction.  A
four-thread paired run improved from 5.898 to 5.385 seconds.  HLV and
reconstruction SHA-256 values remain identical to the original baseline.

### Bulk bit-writer append

Finished temporary writers are now appended a byte at a time.  Aligned
destinations use `memcpy`; unaligned destinations shift one byte at a time and
leave only the final 0..7 bits for the normative scalar writer.

| Metric | Zero/DC baseline | Bulk append | Change |
|---|---:|---:|---:|
| Bit-writer put calls | 683,126,180 | 638,527,925 | -6.53% |
| Bits sent through `put` | 2,470,174,597 | 1,386,949,485 | -43.85% |
| Buffer growths | 5,516,655 | 5,502,528 | -0.26% |
| Primitive operations/frame | 156,022,120 | 151,092,853 | -3.16% |

The 360-frame workload bulk-copied 7,147,571 aligned bytes and bulk-shifted
128,255,568 unaligned bytes.  Its three-run medians were 17.244 seconds
(20.88 fps) with one thread and 5.422 seconds (66.40 fps) with four threads.
A direct paired run improved from 21.349 to 20.153 seconds; reverse-order
timing was affected by the same host-load variance seen in the baseline, while
the operation reduction remained deterministic.

The direct regression covers every destination alignment 0..7 and source
length 0..129 bits.  Full HLV and reconstructed Y4M hashes remain identical.

### Exact palette-impossibility prefilter

Before k-means, each Y/U/V axis is reduced to the minimum number of intervals
needed to cover all samples within the normative maximum errors (Y +/-10,
U/V +/-12).  If any axis needs more than 2/4/8 colors, that palette candidate
cannot pass the existing post-clustering check and is rejected exactly.

| Metric | Bulk-append baseline | Palette prefilter | Change |
|---|---:|---:|---:|
| Palette distance evaluations | 2,952,806,400 | 2,317,479,040 | -21.51% |
| Prefilter samples/bins | 0 / 0 | 33,177,600 / 66,355,200 | new |
| Proven-impossible candidates | 0 | 111,728 | new |
| Primitive operations/frame | 151,092,853 | 133,721,351 | -11.49% |

Two interleaved one-thread A/B pairs averaged 21.281 seconds for the immediate
baseline and 15.930 seconds with the prefilter, a 25.1% time reduction.  A
four-thread paired run improved from 6.070 to 4.986 seconds.  The separate
three-run candidate medians were 18.778 seconds (19.17 fps) and 5.304 seconds
(67.87 fps) under higher concurrent host load.

The palette-2/palette-8 round-trip tests, threaded regression, HLV SHA-256, and
reconstructed Y4M SHA-256 all remain unchanged.

Once palette centers are fixed, final index assignment now stops at the first
sample that violates the same Y/UV limit previously checked in a separate
complete pass:

| Metric | Prefilter only | Early final reject | Change |
|---|---:|---:|---:|
| Palette distance evaluations | 2,317,479,040 | 2,247,849,178 | -3.00% |
| Primitive operations/frame | 133,721,351 | 131,787,188 | -1.45% |

Two interleaved one-thread pairs averaged 19.171 seconds before and 16.908
seconds after the early reject, an 11.8% reduction.  The four-thread pair
improved from 5.051 to 4.691 seconds.  Output hashes and mode decisions remain
identical.
