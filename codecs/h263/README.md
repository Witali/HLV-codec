# 3GP/H.263 profile

This directory contains the portable demultiplexer and decoder used by the
native Windows player and the ESP32-2432S028 firmware.

The first supported profile is intentionally narrow:

- ISO Base Media/3GP container with an `s263` video sample entry;
- baseline H.263 profile 0;
- fixed-rate `176x144` QCIF video;
- YUV 4:2:0 output;
- video playback only (additional container tracks are ignored);
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

The script preserves the source aspect ratio inside the standardized QCIF
canvas, pads with black, emits no audio track, verifies the stream metadata,
and fully decodes the result with FFmpeg.

Run the host smoke test:

```powershell
.\scripts\test_h263_3gp.ps1
```
