# HLV adaptive constant-quality mode

The adaptive constant-quality controller is an encoder-only feature. It emits
ordinary HLV stream-v12 packets, so the decoder performs exactly the same work
as for manually selected quantizers.

## Modes

- `--target-psnr X`: fixed minimum reconstructed YUV420 PSNR per frame.
- `--adaptive-quality`: scene-dependent target between `--psnr-min` and
  `--psnr-max` (defaults 30 and 35 dB).
- `--cq-trials N`: maximum normal quantizer trials per frame (default 5).
- `--cq-log FILE.csv`: per-frame decisions and measurements.

`--bitrate` and constant-quality modes are mutually exclusive.

## Scene analysis

The encoder measures:

1. Luma motion after searching a small integer global translation. This keeps
   slow camera pans in the high-quality class.
2. Luma spatial gradients as a fine-detail estimate.
3. Raw temporal MAD to protect hard scene cuts, which become long-lived
   references.

The normalized stress is approximately:

```text
motion * (0.70 + 0.30 * detail)
```

Consequently static detail does not reduce the target by itself. Fine detail
mainly strengthens the quality reduction when it is also moving.

Target changes are smoothed asymmetrically: quality may decrease relatively
quickly when motion starts, but rises more slowly when the scene becomes calm,
which avoids visible pumping.

## Quantizer search

For each frame the encoder clones its complete predictive state, tries several
quantizers, reconstructs the frame, and measures actual YUV420 PSNR. It chooses
the smallest packet meeting the target. If a SKIP decision causes a sharp
quality discontinuity, the controller first reduces the encoder-only RDO bit
penalty to insert a moderate residual, rather than jumping directly to a large
near-lossless packet.

The selected trial becomes the real predictive state; discarded trials cannot
cause encoder/decoder drift.

## Initial validation (320x240, 25 fps, 20 frames)

With 30..35 dB adaptive targets and five trials:

| Test class | Average target | Average actual |
|---|---:|---:|
| Smooth scene | 35.00 dB | 35.58 dB |
| Dynamic synthetic scene | 34.73 dB | 35.47 dB |
| Slow photo pan | 34.95 dB | 36.68 dB |
| Moving UI | 33.54 dB | 34.08 dB |
| Fine moving texture | 30.82 dB | about 32.8 dB in the first validation run |

The overshoot is intentional when prediction or palette coding provides extra
quality at little or no additional size. Longer real-video validation remains
required.
