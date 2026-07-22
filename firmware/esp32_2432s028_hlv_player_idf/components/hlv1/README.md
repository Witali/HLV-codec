# Vendored HLV1 decoder component

This directory contains the decoder-only snapshot required by the ESP-IDF
firmware.  Keeping the public header and decoder sources here makes the
firmware project buildable without paths, packages or generated files outside
its own directory.

The snapshot is copied from the repository's `include/hlv1.h`,
`src/hlv1_internal.h`, `src/hlv1_common.c` and `src/hlv1_decode.c`.  Encoder and
Y4M sources are deliberately absent; section garbage collection removes the
unused encoder-side helpers which remain in the shared common source.
