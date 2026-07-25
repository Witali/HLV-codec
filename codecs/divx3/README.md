# DivX 3 decoder

This package contains a portable C decoder for Microsoft MPEG-4 v3, the
bitstream commonly stored in AVI files with the `DIV3` or `MP43` FourCC.
It is not the MPEG-4 Part 2 ASP format used by DivX 4 and DivX 5.

The current decoder supports:

- AVI files with `DIV3`, `MP43`, and the common v3 aliases;
- I- and P-pictures in display order;
- all three selectable luma/chroma run-level tables;
- DC and AC prediction, half-pixel motion compensation, and all three AC
  escape forms;
- optional mono 8-bit PCM metadata in the AVI container.

The first embedded profile intentionally excludes B-pictures and per-
macroblock quantizer changes. The portable decoder uses two padded YUV420
reference frames. At 256x144 it reserves 141,008 bytes (about 138 KiB); at
320x180 it reserves about 235 KiB before the AVI packet buffer, so QVGA is not
suitable for the original ESP32 board without a later compact-frame
optimization.

Create the validated 256x144, 12 fps, `q=4` Big Buck Bunny profile with mono
PCM_U8 audio:

```powershell
.\scripts\encode_big_buck_bunny_divx3.ps1
```

The script always starts from the repository's approved 1080p MOV, rejects B
pictures and video packets above the firmware's 96 KiB limit, decodes the
complete result with FFmpeg, and writes the matching `out\play.txt`.

## Build and decode

```sh
make -C codecs/divx3
./codecs/divx3/divx3dec input.avi output.y4m
```

The command writes YUV4MPEG2 and can stream to stdout:

```sh
./codecs/divx3/divx3dec input.avi - | ffplay -
```

## Provenance

The decoder was ported to C from the clean-room, MIT-licensed
[`mgvs/go-msmpeg4`](https://github.com/mgvs/go-msmpeg4) implementation.
`src/divx3_tables.inc` was generated from upstream revision
`c20153cdddc3b72494266e02e95fdb657bbacc56` by
`tools/gen_tables.py`. The upstream license is preserved in
`LICENSE.mgvs-go-msmpeg4`.

FFmpeg is used only as an encoder and pixel oracle by the regression script;
it is not linked into the decoder or firmware.
