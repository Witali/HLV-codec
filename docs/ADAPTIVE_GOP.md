# Adaptive K/P selection and GOP length

HLV v12 can optionally compare a forced P-frame and forced K-frame from the
same predictive encoder state. This is an encoder-only feature: the decoder,
stream syntax, frame headers, and playback cost are unchanged.

## CLI

```sh
./hlvenc input.y4m output.hlv \
  --adaptive-gop \
  --gop 100 \
  --min-key-interval 8 \
  --keyframe-bias 1.00
```

- `--gop` remains the hard maximum interval.
- `--min-key-interval` prevents repeated keyframes during ordinary motion.
- `--keyframe-bias` permits a K-frame when `K_cost <= P_cost * bias`.
  The recommended strict value is `1.00`. Values above 1.00 trade more
  encoder time and keyframes for speculative future-reference benefit.

## Decision process

1. The first frame is always K.
2. Before the minimum interval, P is retained unless the temporal change is
   extremely large.
3. At the maximum interval, K is forced.
4. Otherwise, the encoder is cloned twice.
5. One clone encodes a forced P-frame; the other encodes a forced K-frame.
6. Both candidates are compared using full-frame weighted distortion plus the
   normal HLV bit cost.
7. The selected clone becomes the real predictive state.

The second candidate is discarded. The process is compatible with constant
quality and bounded local two-pass operation because encoder clones include the
complete reconstructed reference and motion-vector state.

## Measured result

On a 320×240, 25 fps screening clip containing five one-second scenes with hard
cuts, strict adaptive GOP selected one K-frame per scene and no extra K-frames
inside continuous two-second clips.

At identical quantization:

| qY | Size change | PSNR change |
|---:|---:|---:|
| 64 | -0.052% | +0.000 dB |
| 96 | -0.060% | +0.000 dB |
| 128 | -0.046% | +0.041 dB |
| 192 | +0.011% | +0.144 dB |

The benefit is modest, but the decoder cost is exactly zero. The fast encoder
screening rate changed from approximately 33.9 fps to 21.3 fps, so the feature
is optional and defaults off.
