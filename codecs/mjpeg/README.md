# MJPEG codec profile

This package defines the interoperable MJPEG profile used by the shared
desktop/device laboratory:

- standard RIFF AVI container;
- baseline `MJPG` video frames;
- source-native constant frame rate;
- 320-pixel video width with the source aspect ratio preserved;
- YUV 4:2:0 JPEG sampling;
- optional unsigned 8-bit mono PCM audio at 16 kHz.

Big Buck Bunny is encoded only from the approved 1080p MOV source. The final
FFmpeg pass encodes MJPEG and the established audio level curve directly into
one AVI file:

```powershell
.\scripts\encode_big_buck_bunny_mjpeg.ps1
```

The default result is
`out\BigBuckBunny_1080p_mjpeg_q5_native-fps_320x180.avi`. The script also
writes `out\play.txt` containing the AVI filename. Copy both files into the
microSD card's `/HLV` directory. `play.txt` is mandatory; the player does not
fall back to a hard-coded filename when it is missing.

The ESP32 implementation uses the chip's ROM TJpgDec decoder and converts its
RGB888 MCU output to a reusable RGB565 frame buffer. No downloaded JPEG
library is required.
