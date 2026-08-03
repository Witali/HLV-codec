# Audio in the HLV-1 container

HLV-1 can carry a single mono audio track without changing the video bitstream
syntax version. Two profiles are defined:

- codec 1: unsigned 8-bit PCM (`PCM_U8`), normally 16 kHz, silence 128;
- codec 2: 4-bit IMA ADPCM (`IMA_ADPCM`), normally 32 kHz, decoded to PCM16.

PCM_U8 at 16 kHz adds 16,000 bytes per second. IMA ADPCM at 32 kHz adds about
16,180 bytes per second, including one independent block header per video
frame, while doubling the audio bandwidth and retaining a PCM16 output path.

## Sequence header

Audio uses bytes that were reserved in the fixed 28-byte HLV-1 header.
All multibyte integers are little-endian.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 5 | 1 | flags; bit 0 (`HLV1_FLAG_AUDIO`) means that audio is present |
| 23 | 1 | audio codec: 0 = none, 1 = `PCM_U8`, 2 = `IMA_ADPCM` |
| 24 | 2 | sample rate in Hz |
| 26 | 1 | channel count; must be 1 for either supported codec |
| 27 | 1 | reserved, must remain zero |

With no audio track, flag bit 0 and bytes 23 through 26 must all be zero.
Unknown flag bits and unsupported audio metadata make the header invalid.

## Frame packets

Every `FRM1` packet contains one compressed video frame followed by the audio
samples belonging to that frame's presentation interval:

```text
20-byte FRM1 header
compressed video: ceil(bit_length / 8) bytes
audio tail: payload_size - ceil(bit_length / 8) bytes
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
ends first, the encoder pads subsequent intervals with PCM silence.

### IMA ADPCM block

Every IMA audio tail is a complete independently decodable block. All
multibyte values are little-endian.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 2 | initial signed PCM16 predictor; also the first output sample |
| 2 | 1 | initial IMA step-table index, 0 through 88 |
| 3 | 1 | reserved, must be zero |
| 4 | 2 | decoded sample count, 1 through 4096 |
| 6 | remaining | standard 4-bit IMA codes, low nibble first |

The exact block size is `6 + floor(sample_count / 2)` bytes. The explicit
sample count allows both odd and even frame intervals. Resetting predictor and
step index in every video packet permits seeking and video-frame dropping
without losing audio state. The ESP32 decoder retains at most 128 compressed
bytes while expanding a block, so block capacity is independent of its
512-byte stdio refill and no complete compressed packet is copied between
tasks.

## Command-line tools

Mux prepared raw audio while encoding:

```powershell
.\build\msvc\hlvenc.exe input.y4m output.hlv `
    --audio-u8 audio.u8 --audio-rate 16000
```

Encode signed little-endian PCM16 as IMA ADPCM:

```powershell
.\build\msvc\hlvenc.exe input.y4m output.hlv `
    --audio-ima-s16le audio.s16le --audio-rate 32000
```

Inspect the track and its byte count:

```powershell
.\build\msvc\hlvinfo.exe output.hlv
```

Extract video and decoded raw audio. The output is PCM_U8 for codec 1 and
PCM_S16LE for IMA codec 2:

```powershell
.\build\msvc\hlvdec.exe output.hlv output.y4m --audio-out output.u8
.\local_tools\ffmpeg\bin\ffmpeg.exe -f u8 -ar 16000 -ac 1 `
    -i output.u8 output.wav
```

For IMA files, use `-f s16le -ar 32000 -ac 1` and a `.s16le` output name.

`scripts/transcode_hlv15.ps1` performs resampling, mono downmixing, PCM16
preparation and IMA muxing automatically. Its default peak-detected level
curve is calibrated to -0.1 dBFS. `-NoAudio` creates a video-only file.
