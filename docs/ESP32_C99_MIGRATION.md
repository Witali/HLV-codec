# ESP32 player C99 migration

## Goal

Rewrite the ESP32-2432S028 video player as C99 while preserving every
supported container, codec, audio path, display path, UART command and
real-time playback behaviour.

The migration is complete only when the application and repository codec
components used by the firmware:

- contain no C++ translation units;
- link without the built-in ESP-IDF `cxx` runtime component;
- contain no `std::`, RTTI, exception, `new`/`delete`, `__cxa` or
  `__gxx_personality` symbols;
- keep application code within the C99 language subset and retain the
  existing Xtensa assembly optimisations. ESP-IDF 5.5.5 itself requires the
  compiler's GNU C17 mode because its public headers use C11
  `_Static_assert`;
- pass the same host, QEMU and physical-board tests as the C++ baseline.

Windows tools are outside this migration. Their implementation language does
not affect the ESP32 firmware acceptance criteria.

## C++ baseline

The branch starts at Git commit `dad6f80`. A complete baseline build was made
before changing any source:

```text
ESP-IDF:       5.5.5
target:        esp32
configuration: sdkconfig + sdkconfig.defaults
app binary:    707952 bytes (0xacd70)
ELF text:      533563 bytes
ELF data:      174268 bytes
ELF bss:       22337 bytes
```

The baseline application binary SHA-256 is:

```text
7C4E00B9F87BBBE5931F8A147C9576B71D7809910C1A9D722AB6DE7B8D525E08
```

Flashable baseline files are preserved outside the worktree under:

```text
out/firmware_baselines/cpp-dad6f80/
```

The baseline ELF contains 34 symbol-table lines associated with the C++
runtime, including `__cxa`, `operator new`, `operator delete`, `std::`, RTTI
and vtables. The final C99 build must reduce this count to zero.

## Migration order

1. Replace configuration namespaces and C++ constants with C99 headers.
2. Replace the display, HLV, BPV, MJPEG and UART classes with explicit C
   context structures and functions.
3. Convert the player scheduler, audio pipeline, container dispatch and
   rendering pipeline.
4. Convert the firmware-facing H.263 and AMR-NB implementations and remove
   the `cxx` component from the link.
5. Convert all QEMU benchmark entry points so the same off-board checks remain
   available.
6. Run the complete host/QEMU suite and the physical A/B test.

Each stage must build before the next one starts. Behavioural changes and
performance optimisations are not mixed into the language migration.

## Current verification status

The production source migration is complete. The ESP-IDF application and
every repository codec implementation linked into it contain only C and
Xtensa assembly translation units. A clean default build and all six
codec-specific benchmark configurations build successfully:

| Configuration | Application binary |
| --- | ---: |
| Default player | 690496 bytes |
| H.263 | 212192 bytes |
| DivX 3 | 195056 bytes |
| HLV | 183568 bytes |
| MPEG-1 | 157632 bytes |
| BPV | 108592 bytes |
| MJPEG | 205136 bytes |

Every configuration links with the C linker driver. Auditing all seven linker
maps for `libstdc++`, `__cxa`, `operator new`, `operator delete`, `std::` and
`__gxx_personality` produces zero matches. The default C99 application binary
is 17456 bytes (2.47%) smaller than the preserved C++ baseline.

The only `.cpp` files below the migrated AMR-NB source tree are the original
Android command-line and gtest test harnesses in
`codecs/amrnb/third_party/pv/dec/test`. They are not compiled into the ESP32
firmware or the Windows player. The Windows player and the superseded Arduino
firmware are separate targets and remain outside this migration.

The physical A/B matrix remains pending while the shared ESP32 board is in use
by another process. No firmware was flashed as part of the source migration.

## Physical A/B matrix

Both the preserved C++ image and the final C99 image must run on the same
ESP32-2432S028 board with the same SD card, clock configuration and input
files.

| Path | Reference file on the SD card | Required checks |
| --- | --- | --- |
| HLV + PCM_U8 | `VID_20260522_181611.hlv` | decode, audio, A/V sync |
| BPV | `VID_20260522_181611.bpv1` | decode and presentation |
| MPEG-1 | `VID_20260522_320x240_mpeg1.mpg` | video and MP2 audio |
| MJPEG/AVI | `VID_20260522_320x240_mjpeg.avi` | strip decode and presentation |
| DivX 3/AVI | `VID_20260522_181611_divx3_q4_12fps_256x144.avi` | predictive decode |
| H.263/3GP | `cif30.3gp` | CIF decode and AMR-NB audio |
| H.263/AVI | `cif30.avi` | CIF decode and PCM audio |

For every row, capture at least 300 consecutive frame records. Compare:

- detected geometry and source frame rate;
- observed frame rate and sequence gaps;
- SD, decode, render, work and present time distributions;
- display skips;
- audio rebuffers, underrun samples, silence chunks and loop events;
- free internal RAM and PSRAM after opening the file.

The C99 result must have no decode errors, frame-sequence gaps, unexpected
display skips or audio underruns. Any performance or memory regression must be
reported explicitly rather than hidden by changing clocks, files or player
settings.
