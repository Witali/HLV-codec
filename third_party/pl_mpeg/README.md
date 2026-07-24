# PL_MPEG

Vendored from <https://github.com/phoboslab/pl_mpeg> at commit
`c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`.

The project uses PL_MPEG as its MPEG-PS demuxer, MPEG-1 Video decoder and
MPEG-1 Audio Layer II decoder under the MIT license.

Local low-memory changes in `pl_mpeg.h`:

- standard declarations include `<stddef.h>` and `<stdio.h>` so a standalone
  C translation unit also compiles with MSVC;
- decoder creation is lazy and respects streams disabled before the first
  metadata/decode query;
- `PLM_VIDEO_NO_B_FRAMES` stores two video frames instead of three and assumes
  that the input contains only I/P pictures;
- allocation failures are returned to the player instead of dereferencing a
  null buffer, and `PLM_VIDEO_MAX_FRAME_BYTES` bounds the padded frame size.

The common implementation defines an 8 KiB input/elementary-stream buffer and
`PLM_VIDEO_NO_B_FRAMES`. It also caps one padded YCbCr frame at 69,120 bytes,
the 240x192 macroblock allocation needed by a visible 240x180 picture.
Therefore MPEG files produced for these players must not contain B pictures
or exceed the 240x180 profile.
