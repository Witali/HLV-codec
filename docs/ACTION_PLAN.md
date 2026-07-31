# HLV-1 maintained action plan

## Current stable format

- Stable stream syntax: **v15**; v14 remains decode-compatible.
- Decoder target: 320×240 at 25 fps on a scalar 100 MHz processor.
- Conservative worst synthetic decoder estimate: about 2.0 million work-cycle
  equivalents per frame, below the 4 million cycle budget. New decoder-side tools should normally keep the codec core below about 2.5–3.0 million cycles/frame, reserving the remaining budget for color conversion, I/O, and scheduling overhead.
- New tools are accepted only when their compression benefit justifies decoder
  work. Encoder-only improvements are preferred.

## Completed and accepted

- [x] Portable C encoder, C decoder, shared `libhlv1`, CLI tools.
- [x] FFmpeg Y4M stdin/stdout pipeline; no FFmpeg patch.
- [x] Stream v2 compact SKIP and macroblock residual flag.
- [x] 8×8 split motion blocks.
- [x] Extended qstep range to 2040.
- [x] One-pixel and half-pixel motion.
- [x] Optional global frame translation.
- [x] Compact residual-block mask.
- [x] Frequent-coefficient VLC.
- [x] Directional intra: DC, vertical, horizontal, plane.
- [x] Median neighboring motion-vector predictor.
- [x] Strict 2/4-color palette blocks.
- [x] Multi-candidate encoder RDO (`fast=1`, `balanced=4`, `slow=8`).
- [x] Perceptually adaptive constant-quality mode, normally 30–35 dB.
- [x] Bounded local two-pass bitrate mode using temporary 10-second windows.
- [x] Optional adaptive K/P selection and adaptive GOP length. Strict bias
      1.00 is accepted; decoder cost is zero and continuous-scene false
      positives were not observed in the screening suite.
- [x] Round-trip, CRC, malformed-stream, ASan, and UBSan tests.
- [x] Stable v14 normative Y7/U6/V6+Q4 reconstructed references and bounded
      7,680-byte ESP32 packet refill.
- [x] v15 zero-payload repeat frames.
- [x] v15 row-bounded `SKIP_RUN` for 2–17 macroblocks.
- [x] v15 four-way prediction with one joint macroblock residual.
- [x] v15 16x8 and 8x16 prediction with one joint macroblock residual.

## Implemented but still requiring broader validation

- [ ] Adaptive-quality mode on long real footage: faces, foliage, water, noise,
      animation, games, and rapid editing.
- [ ] Bounded local two-pass on multiple windows and changing scene complexity.
- [ ] Adaptive GOP combined with constant-quality and local two-pass on long
      real clips.
- [ ] Stable one-pass bitrate controller; the first implementation oscillates
      on mixed material and is not accepted.

## Next encoder-only improvements — zero decoder cost

1. [ ] Improve K/P decision with a short future horizon inside the buffered
       local two-pass window. Test whether evaluating the next 2–8 frames gives
       more benefit than current-frame RDO without analyzing the entire file.
2. [ ] Scene-aware GOP budgets: separate K/P size models and early K only when
       the following reference savings repay its cost.
3. [ ] Within-window bit allocation for the local two-pass mode while retaining
       exact window-size prediction.
4. [ ] Sparse first-pass sampling to reduce multi-probe encoding time.
5. [ ] Frame-adaptive Y/UV qstep allocation.
6. [ ] Perceptual RDO: higher weight for calm edges/text, lower weight for fast
       detailed motion, and a penalty for reference drift.
7. [ ] Repair the one-pass virtual-buffer controller using window-derived size
       models and bounded qstep slew.

## Low decoder-cost candidates

8. [ ] Specialized `DC_ONLY`, `DC_H`, `DC_V`, and `DC_HV` residual tokens.
       Accept only if the size improvement is material and decoder parsing is
       simpler than the generic VLC path.
9. [ ] Select one of a few pre-trained static VLC tables per frame/GOP. Only a
       table index is signaled; no adaptive arithmetic coder.
10. [ ] Byte-aligned macroblock-row restart markers and optional row CRC.
        This targets robustness and seeking rather than PSNR.
11. [ ] Two simple global vectors for upper/lower frame halves. Retain only if
        it clearly improves camera motion without many extra interpolations.

## Moderate decoder-cost experiments

12. [ ] Quarter-pixel bilinear motion. Require at least about 0.4–0.5 dB on
        smooth motion and no significant regression elsewhere.
13. [ ] One additional integer 4×4 DCT-like transform alongside WHT. Measure
        selection frequency, BD-rate, and decoder work.
14. [ ] Very light block-boundary filter. Reject if text blurs or the worst-case
        decoder-core estimate approaches 3 million cycles/frame or the estimated full pipeline approaches 4 million cycles/frame.

## Longer-term experimental ideas

15. [ ] GOP-trained 4×4 residual dictionary with index, sign, transform, and
        power-of-two scale.
16. [ ] 8×8/16×16 absolute texture atlas for UI, games, and animation.
17. [ ] Hybrid/compact reconstructed references, postponed until memory rather
        than coding efficiency becomes the priority.

## Tested and rejected for now

- [x] Additional pair-VLC tree: average file growth around 0.3%.
- [x] Original 16×8 and 8×16 experiment: about -0.06% size and +0.016 dB;
      replaced in v15 by a dedicated joint-residual syntax guarded against
      both size and weighted-distortion regression.
- [x] Separate gradient block: about -0.05% size.
- [x] Broad palette mode: damaged natural imagery; only strict palette retained.
- [x] Aggressive adaptive-keyframe bias 1.03: too many K-frames for negligible
      gain. Strict bias 1.00 retained.

## Benchmark requirements

- [ ] Populate `bench/sources_real` with reproducible open-license footage.
- [ ] Finish five-minute matched-bitrate runs for HLV, MPEG-1/2, MJPEG, H.264,
      VP8/9, and AV1.
- [ ] Record PSNR-Y/U/V/all, SSIM, actual bitrate, encode/decode speed, keyframe
      count, and decoder work counters.
- [ ] Add BD-rate/BD-PSNR curves and worst-frame analysis.
- [ ] Save visual crops and difference frames for artifact review.
