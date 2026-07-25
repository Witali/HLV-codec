# 3GP/H.263 profile

This directory contains the portable demultiplexer and decoder used by the
native Windows player and the ESP32-2432S028 firmware.

The embedded profiles are intentionally bounded:

- ISO Base Media/3GP container with an `s263` video sample entry;
- baseline H.263 at `176x144` QCIF or intra-only H.263+ custom picture format
  at `256x144`, `256x192`, `320x180`, or `320x240`;
- profile 0 in the `d263` sample description;
- YUV 4:2:0 output;
- optional AMR-NB mono audio at 8 kHz through the companion decoder;
- no B pictures and no resolution changes.

The demultiplexer reads `stsz`, `stco`/`co64`, `stsc`, and `stts` tables
directly from the file. Only compact `stsc` and one-entry constant-rate `stts`
state is retained in RAM; per-frame sample sizes and chunk offsets remain in
the 3GP file.

`third_party/pv` is the Apache-2.0 PacketVideo decoder from AOSP. See
`third_party/pv/PROVENANCE.md` and the included `NOTICE`.

Encode any source:

```powershell
.\scripts\encode_h263_3gp.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video.3gp
```

The default is the hardware-verified intra-only `320x240`, 15 fps,
1536 kbit/s profile with a 1024-kbit VBV buffer. RD macroblock decisions,
trellis quantization, and rate-distortion coefficient selection improve
quality without enabling unsupported H.263 bitstream tools. The default
`-FitMode Crop` preserves aspect ratio while filling the canvas and cropping
equal margins from opposite sides. Use `-FitMode Contain` to retain the
complete source with black padding. The script adds AMR-NB audio when the
source has audio, verifies the stream metadata, and fully decodes the result
with FFmpeg. Use `-NoAudio` for a silent file.

Custom H.263+ profiles use GOP 1 regardless of `-Gop`. Keeping every custom
frame intra-coded lets the ESP32 decoder use one YUV420 frame. Its Y, U, and V
planes are allocated independently so 320x240 does not require one contiguous
115,200-byte block. The baseline QCIF profile retains inter-frame prediction
and two frames.

FFmpeg exposes custom picture sizes through its H.263+ encoder but its 3GP
muxer accepts only the generic H.263 codec id. For the four custom profiles,
the script therefore writes a temporary AVI, probes the standards-compatible
bitstream back as H.263, and losslessly remuxes it as an `s263` 3GP track.

Run the host smoke test:

```powershell
.\scripts\test_h263_3gp.ps1
```
