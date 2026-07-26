# H.263 decoder and AVI encoding profile

This directory contains the portable demultiplexer and decoder used by the
native Windows player and the ESP32-2432S028 firmware.

The legacy-compatible decoder profiles are intentionally bounded:

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

New encodes use a narrower rule than the decoder: baseline H.263 in AVI only,
at standard QCIF `176x144` or CIF `352x288`, with the full source frame rate.
H.263+, custom dimensions and 3GP are retained for decoding old assets but
must not be used as new encoding targets.

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
8 kHz. CIF is intra-only for the bounded ESP32 memory profile.

The ESP32 decodes CIF at its native `352x288` size, copies the central
`320x240` area pixel-for-pixel to the panel and performs no scaling. The
Windows Player displays the complete CIF frame.

Run the host smoke test for both standard AVI profiles at 30 fps:

```powershell
.\scripts\test_h263_avi.ps1
```
