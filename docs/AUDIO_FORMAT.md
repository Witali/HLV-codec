# Audio in the HLV-1 container

HLV-1 can carry a single audio track without changing the video bitstream
syntax version.  The first audio profile is deliberately small and easy to
play on a microcontroller:

- codec: unsigned 8-bit PCM (`PCM_U8`);
- channels: one (mono);
- sample rate: stored in the file header; 16 kHz is recommended for ESP32;
- silence level: 128.

This is uncompressed audio.  At 16 kHz it adds exactly 16,000 bytes per second
to the HLV file.  A compressed audio codec can be assigned a new codec number
later without changing the packet layout.

## Sequence header

Audio uses bytes that were reserved in the fixed 28-byte HLV-1 header.
All multibyte integers are little-endian.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 5 | 1 | flags; bit 0 (`HLV1_FLAG_AUDIO`) means that audio is present |
| 23 | 1 | audio codec: 0 = none, 1 = `PCM_U8` |
| 24 | 2 | sample rate in Hz |
| 26 | 1 | channel count; must be 1 for `PCM_U8` |
| 27 | 1 | reserved, must remain zero |

With no audio track, flag bit 0 and bytes 23 through 26 must all be zero.
Unknown flag bits and unsupported audio metadata make the header invalid.

## Frame packets

Every `FRM1` packet contains one compressed video frame followed by the audio
samples belonging to that frame's presentation interval:

```text
20-byte FRM1 header
compressed video: ceil(bit_length / 8) bytes
PCM_U8 audio: payload_size - ceil(bit_length / 8) bytes
```

`bit_length` continues to describe only the compressed video.  `payload_size`
and the packet CRC cover both the video bytes and the audio tail.  Therefore an
older video decoder that respects `bit_length` can decode the picture and
ignore the additional bytes, while new readers can locate audio without an
extra per-packet field.

For zero-based frame number `n`, an encoder assigns this number of samples:

```text
floor((n + 1) * sample_rate * fps_den / fps_num)
- floor(n * sample_rate * fps_den / fps_num)
```

The alternating interval sizes preserve the exact average sample rate for
fractional frame rates and do not accumulate A/V drift.  If the source audio
ends first, the encoder pads subsequent intervals with sample value 128.

## Command-line tools

Mux prepared raw audio while encoding:

```powershell
.\build\msvc\hlvenc.exe input.y4m output.hlv `
    --audio-u8 audio.u8 --audio-rate 16000
```

Inspect the track and its byte count:

```powershell
.\build\msvc\hlvinfo.exe output.hlv
```

Extract video and raw audio:

```powershell
.\build\msvc\hlvdec.exe output.hlv output.y4m --audio-out output.u8
.\local_tools\ffmpeg\bin\ffmpeg.exe -f u8 -ar 16000 -ac 1 `
    -i output.u8 output.wav
```

The ESP32 preparation script performs resampling, mono downmixing and muxing
automatically.  `-NoAudio` creates a video-only file; `-AudioRate` and
`-AudioVolume` control conversion.
