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
macroblock quantizer changes. The default portable decoder keeps two padded
eight-bit YUV420 reference frames and remains the pixel-exact validation
mode. Predictor state is stored in rolling rows instead of full-picture
grids.

`divx3_decoder_create_y6_u5_v5()` enables the constrained-memory mode. It
stores both references as packed Y6/U5/V5 planes with one signed Q4
average-error correction per 8x8 plane block. At 320x240 the exact decoder
owns 237,608 bytes and the compact decoder owns 174,008 bytes. The compact
references are deliberately non-bit-exact; the Q4 correction prevents a
coherent block-average bias while the regular I-frames bound temporal drift.

Compare exact and compact reconstruction, including PSNR by distance from the
preceding I-frame, with:

```powershell
.\scripts\compare_divx3_compact.ps1 -InputFile input.avi
```

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
