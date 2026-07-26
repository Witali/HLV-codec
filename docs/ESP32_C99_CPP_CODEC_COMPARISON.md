# ESP32 C99 versus C++ codec comparison

Date: 2026-07-26

## Test setup

- Board: ESP32-2432S028, ESP32-D0WD-V3 revision 3.1, 240 MHz.
- Serial connection: CH340C on COM8 at 460800 baud.
- C++ baseline: commit `dad6f80`, application binary 707952 bytes,
  SHA-256
  `7C4E00B9F87BBBE5931F8A147C9576B71D7809910C1A9D722AB6DE7B8D525E08`.
- C99 firmware: code through commit `f27e4cb`, application binary 690496
  bytes, SHA-256
  `7AF0ADFFB707D948C52E83639044DA870E0CA893F82213A9A1759B7138742E47`.
- Both images were written at 460800 baud. Esptool verified the bootloader,
  partition table and application hashes before each hard reset.
- Both versions used the same SD card and the same files. Every measured run
  started with an application reset and captured 300 consecutive UART frame
  records.
- A negative delta means that C99 used less time than C++.

Raw timing CSV files are stored locally under `out/esp32_c99_ab`.

## Results

| Path and test file | C++ FPS | C99 FPS | Decode C++ -> C99 | Render C++ -> C99 | Work delta | Skips C++ / C99 | Audio |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| BPV1, `VID_20260522_181611_v4.bpv1` | 30.041 | 29.993 | 5.678 -> 5.677 ms (-0.02%) | 16.714 -> 16.703 ms (-0.07%) | -0.09% | 0 / 0 | pass / pass |
| MJPEG/AVI, `VID_20260522_320x240_mjpeg.avi` | 29.900 | 29.993 | 24.242 -> 24.186 ms (-0.23%) | 1.137 -> 1.131 ms (-0.58%) | -0.28% | 79 / 79 | pass / pass |
| MPEG-1/PS, `VID_20260522_320x240_mpeg1.mpg` | 12.939 | 13.747 | 77.184 -> 72.569 ms (-5.98%) | 36.688 -> 37.998 ms (+3.57%) | -2.90% | 0 / 0 | MP2 failed / failed |
| DivX 3/AVI, `VID_20260522_181611_divx3_q4_12fps_256x144.avi` | 12.005 | 12.005 | 38.517 -> 38.517 ms (0.00%) | 20.147 -> 20.619 ms (+2.34%) | +0.69% | 0 / 0 | pass / pass |
| H.263/3GP+AMR-NB, `vid320max.3gp` | 15.009 | 15.009 | 31.698 -> 31.589 ms (-0.34%) | 20.863 -> 20.388 ms (-2.28%) | -1.11% | 0 / 0 | pass / pass |
| H.263/AVI+PCM, `cif30.avi` | 30.041 | 29.993 | 20.916 -> 20.853 ms (-0.30%) | 19.652 -> 19.079 ms (-2.92%) | -1.57% | 0 / 0 | pass / pass |

All successful runs had zero frame-sequence gaps, audio rebuffers, underrun
samples, silence chunks and loop events.

The MJPEG FPS value does not mean that every frame reached the display. Both
versions skipped 79 of 300 presentations (26.3%) to remain on the playback
clock. BPV1 is the tested 320x240 30 fps path that presents every frame and
keeps audio continuous.

MPEG-1 is the only path with a material C99 speed difference. C99 reduced
average decode time by 5.98% and raised observed throughput by 6.25%, but it
still falls well short of 30 fps. The MP2 audio path did not initialize on
either image.

## Memory-limited paths

HLV could not be benchmarked numerically. Both images failed before the first
frame while allocating packet block 9 of 9 with the 320x180 low-bitrate
`hlvcbr.hlv`; C99 also failed identically with the compact `uartbench.hlv`.
The last comparable `hlvcbr.hlv` diagnostic reported 11104 bytes free with a
6144-byte largest block on C++, versus 5496 bytes free with a 4352-byte
largest block on C99. The functional result is the same, but the lower
remaining C99 heap should be investigated.

The CIF `cif30.3gp` file with AMR-NB also failed before the first frame on both
images with `frame buffer memory`. The same 352x288 H.263 video in
`cif30.avi`, with PCM audio, completed 300 frames at 30 fps on both images.
The additional 3GP/AMR-NB state therefore pushes CIF beyond the current memory
limit; CIF decoding itself is functional.

## Conclusion

The C99 port preserves the observable behaviour and timing of BPV, MJPEG,
DivX 3 and both playable H.263 container paths. Its changes are generally
within approximately 3% of the C++ baseline, with MPEG-1 decode as the useful
exception at about 6% faster.

The remaining failures are shared with the C++ baseline: HLV packet-pool
allocation, CIF H.263/3GP frame-buffer allocation and MPEG-1 MP2 audio
initialization. C99 introduces no new decode errors, frame gaps, display skips
or audio underruns in the playable paths.

After testing, the board was left running the C99 firmware with
`VID_20260522_181611_v4.bpv1` selected.
