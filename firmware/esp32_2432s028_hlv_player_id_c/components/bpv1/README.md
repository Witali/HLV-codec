# BPV1 decoder component

This ESP-IDF component builds the portable decoder from
`codecs/bpv/src/bpv1_decode.c`. The same source is linked into the native
Windows player and covered by the host compatibility tests, preventing the
desktop and embedded bitstream implementations from drifting apart.
