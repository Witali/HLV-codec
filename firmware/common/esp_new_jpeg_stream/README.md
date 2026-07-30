# esp_new_jpeg streaming entropy input

This module adapts the pinned Espressif `esp_new_jpeg` 1.0.2 decoder to a
bounded compressed-input refill buffer. It replaces only
`jpeg_dec_huffman`; header parsing, quantization, IDCT, RGB565 conversion and
block output continue to use the existing component and project
optimizations.

The private offsets are tied to the ESP32 archive shipped with version 1.0.2
and were verified from its DWARF information. The source is derived from the
same Espressif binary under the ESPRESSIF MIT License recorded at
`docs/reverse_engineering/esp_new_jpeg_1.0.2/LICENSE.ESPRESSIF`.

The adapter performs no allocation. Each firmware allocates its configurable
input buffer once at playback start, reuses it for all JPEG packets, and frees
it at playback end. A refill never grows the buffer and never retains a whole
packet. Standard restart markers are kept contiguous when a marker crosses a
refill boundary.

The QEMU regression must use at least one valid JPEG packet larger than the
configured buffer and compare the complete RGB565 checksum against the
unchanged contiguous decoder.
