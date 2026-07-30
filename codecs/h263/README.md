# H.263 and MPEG-4 Simple Profile decoder

This directory contains the portable demultiplexer and decoder used by the
native Windows player and the ESP32-2432S028 firmware.

The legacy-compatible decoder profiles are intentionally bounded:

- ISO Base Media/3GP with an `s263` video sample entry, or AVI with `H263`;
- baseline H.263 at `176x144` QCIF or intra-only `352x288` CIF;
- all H.263+ custom picture formats are rejected;
- MPEG-4 Part 2 Simple Profile I/P video at `320x240` in AVI with the
  `M4S2` FourCC, no B pictures, data partitioning, reverse VLC or scalability;
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

New encodes use a narrower rule than the decoder: baseline H.263 in AVI only,
at standard QCIF `176x144` or CIF `352x288`, with the full source frame rate.
Legacy 3GP is retained only when its H.263 picture is standard QCIF or CIF.
It must not be used as a new encoding target.

New MPEG-4 assets use the bounded `320x240` M4S2/AVI profile. The decoder
keeps two YUV420 frames for predictive pictures and streams every compressed
packet through a reusable 4 KiB refill buffer. Only the small MPEG-4 VOL
decoder configuration may be retained contiguously, with a strict 256-byte
limit.

```powershell
.\scripts\encode_mpeg4_simple_avi.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video_mpeg4_sp.avi
```

Encode any source as standard CIF:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video_cif.avi `
    -Profile 352x288
```

Or select standard QCIF:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\input.mp4 `
    -OutputFile .\out\video_qcif.avi `
    -Profile 176x144
```

The default is CIF. The script reads the source rate and preserves it exactly;
it does not expose an FPS override or a half-rate mode. Sources above 30 fps
are rejected. `-FitMode Crop` fills the 4:3 frame and crops equal margins;
`-FitMode Contain` retains the complete source with black padding. Fitting is
performed at source resolution before one Lanczos downscale to the complete
QCIF/CIF frame. AVI uses the `H263` FourCC and optional PCM S16LE mono audio at
8 kHz. CIF is intra-only for the bounded ESP32 memory profile. Set
`-VideoQuality 1` for the highest constant-quantizer quality, or leave it at
zero to use the profile's bitrate and VBV defaults.

The ESP32 decodes CIF at its native `352x288` size, copies the central
`320x240` area pixel-for-pixel to the panel and performs no scaling. The
Windows Player displays the complete CIF frame.

Run the host smoke test for both standard AVI profiles at 30 fps:

```powershell
.\scripts\test_h263_avi.ps1
```
