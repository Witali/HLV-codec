# Supplied 60-second BPV1 RD measurement

This directory preserves the compact, inspectable results from the supplied
`bpv1_synthetic_64pal_rd_60s_bundle.zip`:

- 320x200 pixels;
- 12 fps, 720 frames, 60 seconds;
- 64 automatically trained palettes, 16 colors per palette;
- 4x4 blocks and four local colors;
- lambda values 0, 16 and 64;
- all three files independently decoded through all 720 frames.

`bpv1_rd_report.json` is the machine-readable report,
`bpv1_rd_curve.csv` is the compact curve, and
`bpv1_rd_all_verification.json` contains the independent verification result.
The Russian narrative is in `BPV1_RATE_DISTORTION_REPORT_ru.md`.

The generated `.bpv1` streams and MP4 previews are not tracked. The supplied
Python generator depended on a missing earlier base script, so it is not
presented here as a reproducible project test. The package's own deterministic
Node.js tests are used for continuous verification.
