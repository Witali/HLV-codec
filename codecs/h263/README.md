# H.263 profiles in 3GP and AVI

This directory contains the portable demultiplexer and decoder used by the
native Windows player and the ESP32-2432S028 firmware.

The embedded profiles are intentionally bounded:

- ISO Base Media/3GP with an `s263` video sample entry, or AVI with `H263`;
- baseline H.263 at `176x144` QCIF or intra-only `352x288` CIF, plus
  intra-only H.263+ custom picture format at `256x144`, `256x192`,
  `320x180`, or `320x240`;
- profile 0 in the `d263` sample description;
- YUV 4:2:0 output;
- AMR-NB mono audio at 8 kHz in 3GP, or PCM S16LE mono at 8 kHz in AVI;
- no B pictures and no resolution changes.

The demultiplexer reads `stsz`, `stco`/`co64`, `stsc`, and `stts` tables.
Sample sizes and chunk offsets are cached when a 3GP file is opened, removing
two random SD seeks from every decoded frame. AVI video and PCM remain
sequential streams through independent file cursors.

The AVI path scans RIFF headers and `idx1` without retaining per-frame state.
Video and PCM are then read sequentially through independent file cursors.
Zero-sized timing chunks emitted by some AVI muxing paths are skipped.

`third_party/pv` is the Apache-2.0 PacketVideo decoder from AOSP. See
`third_party/pv/PROVENANCE.md` and the included `NOTICE`.

Encode any source:

```powershell
.\scripts\encode_h263_3gp.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video.3gp
```

Create the desktop-friendly AVI variant with 16-bit PCM:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video.avi
```

Both scripts default to the hardware-verified intra-only `320x240`, 15 fps,
1536 kbit/s profile with a 1024-kbit VBV buffer. RD macroblock decisions,
trellis quantization, and rate-distortion coefficient selection improve
quality without enabling unsupported H.263 bitstream tools. AVI uses the
`H263` FourCC and PCM S16LE mono at 8 kHz; no intermediate AMR compression is
used. The default
`-FitMode Crop` preserves aspect ratio while filling the canvas and cropping
equal margins from opposite sides. Use `-FitMode Contain` to retain the
complete source with black padding. The script adds AMR-NB audio when the
source has audio, verifies the stream metadata, and fully decodes the result
with FFmpeg. Use `-NoAudio` for a silent file.

For the CIF profile, the active ESP32 picture is the central `320x240` coded
area. To avoid aliasing and unnecessary resampling, the scripts crop or pad the
source to 4:3 at its original large resolution and then perform one Lanczos
downscale to `320x240`, with accurate rounding and full chroma interpolation.
They add 16 black coded pixels on the left and right and 24 above and below to
form the required `352x288` CIF frame. The output remains square-pixel and does
not carry a SAR or DAR override.

All profiles except predictive QCIF use GOP 1 regardless of `-Gop`. In
dual-core mode the ESP32 requests two YUV420 outputs so CPU1 can decode frame
N+1 while CPU0 converts and submits frame N. It automatically falls back to
one output if the second allocation is unavailable. Y, U, and V are separate
allocations, so 320x240 never requires one contiguous 115,200-byte block.
Predictive QCIF also uses two outputs to preserve its reference frame. CIF is
decoded at its native square-pixel `352x288` coded size. The ESP32 always
copies the centred `320x240` coded area pixel-for-pixel and performs no CIF
scaling. The Windows Player displays the full bordered frame.

FFmpeg exposes custom picture sizes through its H.263+ encoder but its 3GP
muxer accepts only the generic H.263 codec id. For the four custom profiles,
the script therefore writes a temporary AVI, probes the standards-compatible
bitstream back as H.263, and losslessly remuxes it as an `s263` 3GP track.

Run the host smoke test for all 3GP profiles plus AVI/PCM:

```powershell
.\scripts\test_h263_3gp.ps1
```
