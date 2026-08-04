# Multi-codec device lab

This branch is structured as a shared laboratory for comparing multiple video
codecs on desktop, QEMU, and real embedded hardware. Codec implementations
live under [`codecs/`](codecs/); source preparation, benchmark automation,
firmware, QEMU tests, and reports are shared so measurements stay comparable.

HLV-1 now lives in [`codecs/hlv/`](codecs/hlv/). It is an experimental
low-complexity audio/video format for QVGA-class displays and
microcontrollers. Its package contains the portable C encoder and decoder, a
static library, Y4M/PCM tools, correctness tests, and the native Windows player
source.

The second device codec is the standard
[`AVI/MJPEG profile`](codecs/mjpeg/). Its Big Buck Bunny preset keeps a
320-pixel width, preserves the 16:9 aspect ratio (`320x180`), retains the
native source frame rate, and applies the same saved audio level curve as the
HLV preset. New production files use standard mono WAV IMA ADPCM while
preserving source rates up to 48 kHz; legacy PCM_U8 files remain readable.

[`DivX 3`](codecs/divx3/) support decodes the Microsoft MPEG-4 v3 bitstream
found in `DIV3`/`MP43` AVI files. The memory-bounded ESP32 profile accepts up
to 320x240 at 12 fps with I/P pictures only and optional mono WAV IMA ADPCM
audio at the source rate up to 48 kHz. Legacy PCM_U8 audio remains readable. It
stores predictive references as Y6/U5/V5 with Q4 block-average corrections;
the two references are allocated independently so QVGA fits fragmented
internal RAM. The portable decoder retains a pixel-exact 8-bit validation
mode. Physical-board tests decode both 320x180 and 320x240 without sequence or
audio errors. The optimized player sustains 12 fps at 320x180 with no display
skips; QVGA reaches 11.83 fps with 5 skips in a 300-frame run.
DivX 4 and 5 use the different MPEG-4 Part 2 ASP format and are not yet
supported.

The standard [`MPEG-1 profile`](docs/MPEG1_PROFILE.md) uses an MPEG Program
Stream with MPEG-1 Video and MP2 audio. Its ESP32 memory-bounded variant
accepts pictures up to 320x240, contains only I/P pictures, and stores two
packed Y6/U5/V5 reference frames plus one 16-row work area. The same files
play in the native Windows application with ordinary 8-bit YUV frames.

The current [`H.263 encoding profile`](codecs/h263/) uses only baseline H.263
in AVI at the standard `176x144` QCIF or `352x288` CIF picture size. The
encoder preserves the full source frame rate and refuses sources above the
supported 30 fps limit instead of silently halving their rate. It crops or
pads to 4:3 at source resolution, performs one anti-aliased Lanczos downscale
to the complete QCIF/CIF frame, and can add mono WAV IMA ADPCM audio at the
source rate up to 48 kHz. Legacy PCM S16LE mono at 8 kHz remains readable.
CIF remains intra-only for the bounded ESP32 memory profile. The ESP32 displays
the central `320x240` portion of CIF without scaling; the Windows Player shows
the complete `352x288` frame.

The decoders retain compatibility with older 3GP/AMR-NB files only when the
H.263 picture uses standard QCIF or CIF geometry. H.263+ custom-size files are
rejected, and 3GP remains a decoding-only legacy container.

The ESP32 and Windows players also accept bounded MPEG-4 Part 2 Simple
Profile video in M4S2 AVI: `320x240` YUV420, I/P pictures only, at up to
30 fps. The ESP32 retains the previous and current pictures as two compact
Y6/U5/V5 frames with Q4 block-average corrections and reconstructs through a
single 16-row byte-planar workspace. Compressed packets use the same fixed
4 KiB refill path as H.263.

[`BPV1 v6`](codecs/bpv/) is also available as a BPAL-derived experimental
codec. It uses 4x4 blocks, 64 shared 16-color palettes, a two-bit map of four
block modes, one-byte exact block motion, a full-block dictionary and unified
2/4/7/9-byte RAW records. The package includes a bounded-memory,
eight-thread C11 encoder, automatic palette training, encoder-side
rate-distortion selection, streaming validation, Y4M adapters and the supplied
60-second reference measurements. The same portable C decoder is linked into
the native Windows and ESP32 players. BPV1 v6/v7 can carry legacy PCM_U8 or
independent IMA ADPCM mono blocks; production transcodes use IMA at 32 kHz.
Video-only files use the players' frame timer.

## HLV v15 stable format

HLV v15 is the current stable, standalone format. Encoders emit **stream
v15** by default, while players accept v14 and v15; v1-v13 decoding is not
part of the stable implementation. v15 preserves v14 reconstruction and adds
shorter paths for common predictive pictures:

- zero-payload `REPEAT` frames when reconstruction is unchanged;
- row-bounded runs of 2–17 zero-motion `SKIP` macroblocks;
- four 8×8 predictors with one joint 16×16 residual;
- two 16×8 or 8×16 predictors with one joint 16×16 residual.

The encoder admits the new split modes only when they are no larger and have
no greater weighted reconstruction error than the best legacy v14 candidate.
The common v14/v15 syntax also includes:

- short `SKIP` and zero-residual paths;
- 16×16 and optional four-way 8×8 motion prediction;
- integer, half-pixel, and optional global motion;
- extended quantizer range;
- compact residual masks and coefficient VLC;
- directional intra prediction;
- strict 2/4/8-colour palette blocks;
- byte-aligned Y7/U6/V6 literal macroblocks for bounded decode time;
- normative Y7/U6/V6 reference reconstruction with separate signed Q4
  local-average corrections for every 8×8 Y, U and V block, including
  LITERAL blocks, shared bit-for-bit by the encoder, Windows and ESP32
  decoders;
- bounded packet streaming on ESP32 through one reusable 7,680-byte buffer;
- complexity-aware RDO that trades a configurable amount of bitrate for
  lower decoder work;
- the original simple 4×4 integer WHT reconstruction.

The container can also carry unsigned 8-bit mono PCM in each video frame
packet. At the ESP32 default of 16 kHz it costs 16 KB/s; the player converts
it to signed PCM16 for the chip's I2S0 PCM-to-PDM output. See
[`docs/AUDIO_FORMAT.md`](docs/AUDIO_FORMAT.md).

## Build and verify

On Windows, run the repository setup first:

```powershell
.\setup.ps1
```

It verifies or installs the Visual Studio C++ workload and places FFmpeg,
Python 3.12 plus the benchmark packages, and the complete ESP32 toolchain
inside this repository. Re-running it is safe. To prohibit automatic Visual
Studio installation while still checking for an existing compiler, pass
`-SkipVisualStudioInstall`.

Run repository Python helpers through the local environment as follows:

```powershell
.\scripts\python.ps1 .\scripts\compare_hlv_versions.py --help
```

```sh
make -j
make test
make sanitize
```

`make test` covers v14/v15 round-trip, forced `REPEAT`, `SKIP_RUN`,
`FILL`/`SKIP`/split/palette/literal
paths, encoder-state cloning, malformed headers, truncated packets, CRC
errors, and invalid frame ordering. `make sanitize` repeats the tests with
AddressSanitizer and UndefinedBehaviorSanitizer.

Build and test the desktop tools with MSVC:

```powershell
.\scripts\build_msvc.ps1
.\scripts\build_bpv_msvc.ps1
.\scripts\test_divx3.ps1
```

Encode the approved 1080p Big Buck Bunny source to BPV1 v6 at 320x180 and its
native 24 fps. The script uses eight C GOP workers by default:

```powershell
.\scripts\encode_big_buck_bunny_bpv.ps1
```

Create the conservative DivX 3 AVI profile and its matching `play.txt`:

```powershell
.\scripts\encode_big_buck_bunny_divx3.ps1
```

The default profile is 320x240 at 12 fps, preserves the source aspect ratio
with letterboxing, uses I/P pictures only and mono WAV IMA ADPCM audio at
the source rate, up to 48 kHz.

## Native Windows player

The desktop build produces `build\msvc\hlvplay.exe`, a native
HLV/BPV/MPEG-1/H.263 player
that uses Windows D3D11 with an automatic GDI fallback and `waveOut`; FFmpeg
and external codec packs are not needed at runtime. It plays embedded HLV/BPV
PCM_U8 or PCM16-decoded IMA ADPCM mono, preserves the video aspect ratio, and supports pause/resume,
keyframe-aware timeline seeking, native-size centred display and drag-and-drop:

```powershell
.\scripts\build_windows_player.ps1
.\build\msvc\hlvplay.exe .\out\video.hlv
.\build\msvc\hlvplay.exe `
    .\out\BigBuckBunny_1080p_bpv1_v2_lambda64_native-fps_320x180.bpv1
.\build\msvc\hlvplay.exe .\out\video.mpg
.\build\msvc\hlvplay.exe .\out\video.3gp
.\build\msvc\hlvplay.exe .\out\video.avi
```

See [`docs/WINDOWS_PLAYER.md`](docs/WINDOWS_PLAYER.md) for controls and the
headless full-file validation mode.

## ESP32-2432S028 playback

The pure ESP-IDF CYD2USB firmware reads HLV-1, AVI/MJPEG, DivX 3/AVI, BPV1,
the constrained MPEG-1/MP2 profile, or the bounded 3GP/AVI H.263 profiles
from a FAT32 microSD card over an independent SPI3/VSPI DMA bus and displays
it on the 320x240 ST7789 over SPI2 DMA. HLV/BPV IMA ADPCM is decoded directly
to PCM16; legacy PCM_U8 and decoded MPEG MP2 share the same bounded queue and
play through I2S0 PCM-to-PDM data on GPIO26 and the onboard
amplifier; GPIO22 carries the required PDM clock. All four AVI video codecs
use one C99 RIFF/AVI demultiplexer, which routes encoded video and audio chunks
without decoding either payload. Legacy AVI PCM remains readable; production
AVI audio uses mono WAV IMA ADPCM. Each 1024-byte block expands into the bounded
PCM16 queue through a 128-byte compressed refill buffer. The BPV1 path keeps
two compact 9-byte block-record frames. MJPEG converts each decoded MCU row
into one 16-row RGB565 strip. Both paths render through the existing display
DMA strips without allocating a full RGB framebuffer. Arduino and LovyanGFX
are not part of the build.
The pinned ESP-IDF, Python environment and ESP32 toolchain live inside the
firmware project directory:

```powershell
.\setup.ps1
.\scripts\fetch_big_buck_bunny.ps1
.\scripts\prepare_esp32_video.ps1 -InputFile `
    .\out\sources\BigBuckBunny_320x180.mp4 -Width 320 -Height 180
.\scripts\build_esp32.ps1
.\scripts\upload_esp32.ps1 -Port COM8
```

The player reads one filename from `/sdcard/HLV/play.txt`; it never guesses a
fallback video. The selected `.hlv`, MJPEG/DivX 3 `.avi`, `.bpv1`, or
MPEG-1/3GP file must be in the same directory. With the firmware running
normally, the video and selection file can be copied over the CH340C UART
without removing the card:

A short BOOT press opens an alphabetically sorted on-device browser showing
five left-aligned filenames at once. The selected row stays in the middle when
there are at least two filenames on either side. Further short presses advance
the selection; holding BOOT for 800 ms saves it to `play.txt` and starts it.

```powershell
.\scripts\upload_video_uart.ps1 -Port COM8 `
    -File .\out\BigBuckBunny_1080p_mjpeg_q5_native-fps_320x180.avi
.\scripts\upload_video_uart.ps1 -Port COM8 -File .\out\play.txt
```

See [`docs/ESP32_PLAYER.md`](docs/ESP32_PLAYER.md) for the SD-card and upload
instructions. The measured MJPEG decoder backlog and hardware acceptance
criteria are tracked in
[`docs/MJPEG_DECODER_OPTIMIZATION_TODO.md`](docs/MJPEG_DECODER_OPTIMIZATION_TODO.md).

Create a standard CIF H.263 AVI from the required 1080p MOV source:

```powershell
.\scripts\encode_big_buck_bunny_h263_avi.ps1
```

Create a baseline CIF H.263 AVI with source-rate IMA ADPCM audio:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\input.mp4 -Profile 352x288
```

Create a `320x240` MPEG-4 Simple Profile M4S2 AVI with source-rate IMA ADPCM:

```powershell
.\scripts\encode_mpeg4_simple_avi.ps1 -InputFile .\input.mp4
```

For an explicit ESP32 decoder-speed trade-off, use
`-Preset Esp32Speed`. The default Q5 profile is calibrated to the accepted
`Danila_320x240_30fps_MPEG4SP_35dB.avi` reference and uses
`_MPEG4SP_35dB` in converted-video filenames. The speed wrapper selects Q7
and uses `_MPEG4SP_SPEED_q7` instead:

```powershell
.\scripts\transcode_mpeg4_simple.ps1 .\input.mp4 -Preset Esp32Speed
```

Generate the deterministic picture-rich regression video once and encode it
in every production format under the matching `out` directories:

```powershell
.\scripts\generate_all_video_formats.ps1
```

The default H.263 quality is constant-quality Q6. Pass `-VideoQuality 0`
together with explicit bitrate and buffer values only for a deliberate
CBR/VBV encode.

The v14/v15 literal/palette syntax and the decoder-cycle term used by RDO are
described in [`docs/LITERAL_PALETTE_RDO.md`](docs/LITERAL_PALETTE_RDO.md).
The v15 additions are specified in
[`docs/HLV_V15_FORMAT.md`](docs/HLV_V15_FORMAT.md).

## FFmpeg pipe encoding

The source aspect ratio can be preserved by scaling and padding:

```sh
ffmpeg -hide_banner -loglevel error -i input.mp4 -an \
  -vf "fps=15,scale=320:240:force_original_aspect_ratio=decrease:flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./codecs/hlv/hlvenc - output.hlv --preset balanced --quality 55
```

The checked-in Big Buck Bunny v15 profile uses only the project-approved
1080p MOV source, its native frame rate and four GOP workers. The previous
audio-filter chain is replaced by one smooth, peak-detected level curve
(`-20 dB` threshold, `1.6:1` ratio and a wide soft knee). Quiet levels are
raised relative to loud ones. A measurement pass calibrates the curve's
built-in makeup so the actual source peak lands at -0.1 dBFS without a
separate volume filter or limiter.

```powershell
.\scripts\encode_big_buck_bunny_v15.ps1
```

Use `-Fps 1..30`, `-Threads`, or `-OutputFile` to override those three output
parameters. `-Fps 0` (the default) preserves the source frame rate without an
FPS conversion filter. `-MaxFrames` is available for a short smoke test, and
`-DisableSimd` forces the scalar encoder fallback.

Presets:

- `fast`: no motion search, GOP 24;
- `balanced`: motion radius 4 pixels, GOP 30;
- `slow`: motion radius 8 pixels, GOP 45.

Ordinary fixed-quality encoding runs four independent GOP workers by default.
Use `--threads 1..8` to change the worker count. Packets, reconstructed frames
and audio are joined in presentation order, so `--threads 1` and
`--threads 4` produce byte-identical output. Adaptive-quality and bitrate
controllers retain one worker because their per-frame feedback is sequential.
On x86 hosts, exact SSE2 SAD and RDO paths are selected at runtime;
`--simd off` forces the retained scalar pipeline. Run `make test-threaded` to
check the one/four/default/SIMD-fallback equivalence. Benchmark details are in
[`docs/ENCODER_SIMD.md`](docs/ENCODER_SIMD.md).

Stable HLV accepts `--syntax 14` and `--syntax 15`; v15 is the default.

## Constant and adaptive quality

Fixed minimum frame quality, measured as reconstructed YUV420 PSNR:

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./codecs/hlv/hlvenc - output.hlv --target-psnr 35 --cq-trials 5
```

Perceptually adaptive quality keeps calm/predictable frames near 35 dB and
allows fast, highly detailed material to fall toward 30 dB:

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./codecs/hlv/hlvenc - output.hlv \
    --adaptive-quality --psnr-min 30 --psnr-max 35 \
    --cq-trials 5 --cq-log quality.csv
```

The analysis compensates a small global translation before measuring motion,
so a slow camera pan remains a high-quality scene. Static fine detail also
stays sharp; detail lowers the target mainly when it moves. Trial encodes and
RDO adjustments are encoder-only and do not change decoder complexity or the
v12 bitstream.

## Optional adaptive K/P and GOP

HLV can compare a forced P-frame and K-frame from the same cloned predictive
state. This is an encoder-only decision and does not add syntax or decoder
work:

```sh
./codecs/hlv/hlvenc input.y4m output.hlv \
  --adaptive-gop --gop 100 \
  --min-key-interval 8 --keyframe-bias 1.00
```

`--gop` is the hard maximum interval. The strict default bias `1.00` selects a
K-frame only when its current RDO cost is no greater than the P candidate. The
feature is optional because it increases encode time. See
`docs/ADAPTIVE_GOP.md` for measurements.

## Bounded local two-pass bitrate mode

A pipe cannot be rewound, so HLV uses a rolling fragment analysis rather than
requiring a first pass over the complete input. The current fragment is spooled
to a temporary raw-YUV file, probed from a clone of the exact encoder state,
and then immediately encoded and discarded:

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./codecs/hlv/hlvenc - output.hlv \
    --bitrate 400 \
    --two-pass-window 10 \
    --two-pass-trials 5 \
    --two-pass-log windows.csv
```

Only the current 10-second fragment is retained. The first stable version
selects one qstep for the complete fragment, which makes the output pass
reproduce the selected probe size exactly. Later work will test cautious
within-window allocation without sacrificing bitrate accuracy.

## Decode through FFmpeg

```sh
./codecs/hlv/hlvdec output.hlv - \
| ffmpeg -hide_banner -loglevel error -f yuv4mpegpipe -i - decoded.mp4
```

Direct playback:

```sh
./codecs/hlv/hlvdec output.hlv - | ffplay -f yuv4mpegpipe -i -
```

Both tools accept `-` for stdin or stdout. A stream written to stdout has `frame_count=0`; decoders read packets until EOF. Diagnostics are written to stderr only.

## Reproducible local suite

Generate five synthetic five-minute sources covering motion, fine texture, smooth imagery, screen content, and photographic panning:

```sh
python3 scripts/make_local_suite.py --duration 300 --fps 10
```

Run a small but complete five-minute sweep that can resume after interruption:

```sh
python3 scripts/benchmark.py \
  --sources bench/sources/*_5min.mp4 \
  --duration 300 --fps 10 \
  --hlv-syntaxes 2 --hlv-presets balanced --hlv-qualities 40,55,70 \
  --codecs bpv,mjpeg,h264,vp8 --bpv-lambdas 0,16,64 \
  --mjpeg-values 2,6,12 --h264-values 18,26,34 --vp8-values 10,28,46 \
  --prefix local_5min --keep-files --resume
```

`bpv` runs the BPV1 v6 Y4M adapters and records results as `BPV1-v6`.
Palette training keeps the normalized RGBA sequence in host memory, so begin
with a short duration before scheduling long BPV runs. The matched-bitrate
tool also accepts `--codecs bpv`; it searches lambda from
`--bpv-max-lambda` down to zero.

MPEG-1/2 accept only standardized frame rates in FFmpeg. Compare them in a separate 25 fps run:

```sh
python3 scripts/benchmark.py \
  --sources bench/sources/*_5min.mp4 \
  --duration 60 --fps 25 \
  --hlv-syntaxes 2 --hlv-presets balanced --hlv-qualities 40,55,70 \
  --codecs mpeg1,mpeg2 --mpeg1-values 2,6,12 --mpeg2-values 2,6,12 \
  --prefix mpeg_25fps --keep-files --resume
```

The harness normalizes each source once to an FFV1 reference. HLV and every comparator then receive exactly those reconstructed frames. Results are atomically saved after every completed point.

Create a Markdown summary:

```sh
python3 scripts/summarize_results.py bench/results/local_5min.json
```

## Open movie sources

`bench/OPEN_SOURCES.md` lists recommended open material. The helper below downloads the two sources whose direct Wikimedia URLs are included in this release:

```sh
python3 scripts/fetch_open_sources.py
```

The project does not automate downloading arbitrary YouTube material. Keep the required attribution when redistributing open movies.
