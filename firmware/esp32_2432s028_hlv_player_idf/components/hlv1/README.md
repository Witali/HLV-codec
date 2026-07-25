# Vendored HLV1 decoder component

This directory contains the decoder-only snapshot required by the ESP-IDF
firmware.  Keeping the public header and decoder sources here makes the
firmware project buildable without paths, packages or generated files outside
its own directory.

The snapshot is copied from the repository's `codecs/hlv/include/hlv1.h`,
`codecs/hlv/src/hlv1_internal.h`, `codecs/hlv/src/hlv1_common.c` and
`codecs/hlv/src/hlv1_decode.c`. Encoder and Y4M sources are deliberately
absent; section garbage collection removes the unused encoder-side helpers
which remain in the shared common source.

The snapshot accepts standalone syntax v14 only. Compact references store
Y6/U5/V5 samples plus one signed Q4 local-average correction per 8x8 plane block. Motion
compensation, intra prediction and display expansion apply the same
deterministic correction, while zero-motion `SKIP` copies it with the packed
macroblock. The v13 `LITERAL` path copies byte-aligned rows directly and
records zero correction because those samples are already quantized by the
stream syntax.

Y6/U5/V5+Q4 is the normative v14 predictive reference, not an ESP32-only
approximation. The expanded decoder applies the same quantization after every
macroblock and therefore produces exactly the same samples as compact storage.

The ESP32 front end uses `hlv1_decoder_decode_stream()` with two alternating
caller-owned buffers. The bitreader requests sequential spans through an
exact-read callback, updates CRC-32 during refill, drains the packet tail and
returns with the source positioned at the next frame header. This keeps packet
memory bounded without changing the HLV container or decoded reconstruction.
