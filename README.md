# HLV-1 v0.3 development build

HLV-1 is an experimental low-complexity audio/video format for QVGA-class
displays and microcontrollers. This build contains a portable C encoder and
decoder, a static library, Y4M/PCM tools, correctness tests, and a resumable
benchmark harness.

The decoder accepts stream syntax v1 through v13. New encodes use **stream
v13** by default.  The stable syntax now includes:

- short `SKIP` and zero-residual paths;
- 16×16 and optional four-way 8×8 motion prediction;
- integer, half-pixel, and optional global motion;
- extended quantizer range;
- compact residual masks and coefficient VLC;
- directional intra prediction;
- strict 2/4/8-colour palette blocks;
- byte-aligned Y6/U5/V5 literal macroblocks for bounded decode time;
- complexity-aware RDO that trades a configurable amount of bitrate for
  lower decoder work;
- the original simple 4×4 integer WHT reconstruction.

The container can also carry unsigned 8-bit mono PCM in each video frame
packet. At the ESP32 default of 16 kHz it costs 16 KB/s and can be sent
directly to the chip's DAC. See [`docs/AUDIO_FORMAT.md`](docs/AUDIO_FORMAT.md).

## Build and verify

On Windows, run the repository setup first:

```powershell
.\setup.ps1
```

It verifies or installs the Visual Studio C++ workload and places FFmpeg,
Python 3.11 plus the benchmark packages, and the complete ESP32 toolchain
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

`make test` covers v1-v13 round-trip, forced `FILL`/`SKIP`/split/palette/literal
paths, encoder-state cloning, malformed headers, truncated packets, CRC
errors, and invalid frame ordering. `make sanitize` repeats the tests with
AddressSanitizer and UndefinedBehaviorSanitizer.

Build and test the desktop tools with MSVC:

```powershell
.\scripts\build_msvc.ps1
```

## Native Windows player

The desktop build produces `build\msvc\hlvplay.exe`, a native HLV player that
uses only Windows GDI and `waveOut`; FFmpeg and external codec packs are not
needed at runtime. It plays the embedded PCM_U8 mono track, preserves the video
aspect ratio, supports pause/resume, native-size centred display and
drag-and-drop:

```powershell
.\scripts\build_windows_player.ps1
.\build\msvc\hlvplay.exe .\out\video.hlv
```

See [`docs/WINDOWS_PLAYER.md`](docs/WINDOWS_PLAYER.md) for controls and the
headless full-file validation mode.

## ESP32-2432S028 playback

The pure ESP-IDF CYD2USB firmware reads HLV-1 from a FAT32 microSD card over an
independent SPI3/VSPI DMA bus, displays it on the 320x240 ST7789 over SPI2 DMA,
and plays its mono track through DAC GPIO26 DMA and the onboard amplifier. The
included Big Buck Bunny profile preserves the official 320x180 resolution and
centres it without scaling. Its ESP32-specific decoder reads packets into a
reusable 9 x 7680-byte DMA block pool, so a frame never needs one large
contiguous payload allocation. The current test configuration also packs both
predictive frames as Y6/U5/V5 4:2:0, reducing their storage plus working rows
from 184,320 to 138,240 bytes. Arduino and LovyanGFX are not part of the build.
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

See [`docs/ESP32_PLAYER.md`](docs/ESP32_PLAYER.md) for the SD-card and upload
instructions.

The v13 literal/palette syntax and the decoder-cycle term used by RDO are
described in [`docs/LITERAL_PALETTE_RDO.md`](docs/LITERAL_PALETTE_RDO.md).

## FFmpeg pipe encoding

The source aspect ratio can be preserved by scaling and padding:

```sh
ffmpeg -hide_banner -loglevel error -i input.mp4 -an \
  -vf "fps=15,scale=320:240:force_original_aspect_ratio=decrease:flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./hlvenc - output.hlv --preset balanced --quality 55
```

Presets:

- `fast`: no motion search, GOP 24;
- `balanced`: motion radius 4 pixels, GOP 30;
- `slow`: motion radius 8 pixels, GOP 45.

Use older `--syntax N` values only for legacy comparisons. The default is `--syntax 12`.

## Constant and adaptive quality

Fixed minimum frame quality, measured as reconstructed YUV420 PSNR:

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./hlvenc - output.hlv --target-psnr 35 --cq-trials 5
```

Perceptually adaptive quality keeps calm/predictable frames near 35 dB and
allows fast, highly detailed material to fall toward 30 dB:

```sh
ffmpeg -i input.mp4 -vf "scale=320:240,fps=25,format=yuv420p" \
  -f yuv4mpegpipe - \
| ./hlvenc - output.hlv \
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
./hlvenc input.y4m output.hlv \
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
| ./hlvenc - output.hlv \
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
./hlvdec output.hlv - \
| ffmpeg -hide_banner -loglevel error -f yuv4mpegpipe -i - decoded.mp4
```

Direct playback:

```sh
./hlvdec output.hlv - | ffplay -f yuv4mpegpipe -i -
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
  --codecs mjpeg,h264,vp8 \
  --mjpeg-values 2,6,12 --h264-values 18,26,34 --vp8-values 10,28,46 \
  --prefix local_5min --keep-files --resume
```

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
