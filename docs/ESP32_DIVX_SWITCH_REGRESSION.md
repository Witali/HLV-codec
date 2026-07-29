# ESP32 DivX codec-switch regression

Test date: 2026-07-29

## Symptom and cause

Starting the compact QVGA DivX 3 decoder after an AVI with audio could fail
even though the same DivX file worked after a cold boot. The first simultaneous
audio `FILE` expands picolibc's persistent stdio pool. When that allocation
happened while the previous decoder occupied most of DRAM, the retained block
fragmented the heap so the two compact DivX reference frames no longer fit.

The decoder now allocates reference planes separately and reserves both luma
planes first. The firmware also primes the extra stdio slot once while two
QVGA luma-sized regions are reserved. This changes neither stored pixel
precision nor reconstructed output.

## Results

The regression sequence used the existing SD-card assets
`BigBuckBunny_352x288_24fps_5min_H263_CIF_q6.avi` followed by
`BigBuckBunny_320x240_12fps_DivX3_41dB.avi`.

| Target | Result |
| --- | --- |
| QEMU, old C firmware | First DivX reference allocation left a 69,632-byte largest block; the second 83,400-byte allocation failed |
| QEMU, fixed C firmware | 110 DivX frames after the switch, no panic or allocation failure |
| QEMU, fixed C++ firmware | 107 DivX frames after the switch, no panic or allocation failure |
| Physical ESP32, fixed C firmware | 145 H.263 frames in 6 s, then 97 DivX frames in 8 s (about 12.1 FPS), zero reported errors |

The final C application image was flashed through COM8 at 460800 baud. Its
SHA-256 is
`F1AFAD7A0C307C1B371BBAD2A73F7BC3EA3712657CEEC3FDB2B59F3EBCAA5A1E`.
After the test, the original BPV selection was restored (`73` frames observed
in `3` seconds). No test video files were added to the SD card, and a fresh
directory listing still reported `50` files.

QEMU uses the ESP32 address map, linked memory layout, allocator and firmware
heap limits, so it can reproduce this class of exhaustion and fragmentation.
It does not model every DMA, cache or peripheral timing effect exactly;
therefore the physical-board switch test remains part of this regression.

## Portable decoder regression

`scripts/test_divx3.ps1` remained pixel-exact, with decoded SHA-256
`33EEFC63926607C493A0BA518A9B8941AB1947C5E985BA667B311D04933C9934`.
At 320x240, compact-reference memory is 178,176 bytes versus 241,776 bytes for
exact references. The compact comparison reported 43.2551 dB overall PSNR;
the allocation-layout change did not alter the compact representation.
