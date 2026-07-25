# Codec packages

This directory contains the codec implementations exercised by the shared
device and desktop test infrastructure.

Each first-party codec package should own its portable library, public
headers, command-line tools, codec-specific tests, and build metadata. Shared
source preparation, benchmarking, QEMU/device automation, firmware, and
result reporting stay at the repository root so every codec is tested through
the same pipeline.

Current packages:

- [`hlv/`](hlv/) — the HLV-1 encoder, decoder, tools, tests, and native Windows
  player source.
- [`mjpeg/`](mjpeg/) — the standard AVI/MJPEG device profile and its encoding
  workflow.
- [`bpv/`](bpv/) — the BPV1 v2 BPAL-derived reference encoder/decoder,
  automatic 64-palette training, RD selection, Y4M CLI adapters, tests, and
  supplied reference measurements.
- [`divx3/`](divx3/) — a portable Microsoft MPEG-4 v3 (`DIV3`/`MP43`)
  AVI decoder, Y4M command-line adapter, and pixel-exact regression path.

When another codec is added, place it in `codecs/<codec-name>/` and expose a
non-interactive encoder and decoder that the common benchmark adapter can
invoke.
