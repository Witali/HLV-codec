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

The snapshot accepts syntax v1-v13. Its compact v13 `LITERAL` path copies
byte-aligned Y6/U5/V5 rows directly into the predictive planes.
