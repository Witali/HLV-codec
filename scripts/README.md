# Project scripts

This directory contains the Windows setup, build, encoding, test, benchmark,
and ESP32 deployment entry points. Run the PowerShell examples from the
repository root.

## Quick start

Prepare the repository-local toolchains and build the Windows tools:

```powershell
.\scripts\setup.ps1
.\scripts\build_msvc.ps1
```

Build the pure ESP-IDF firmware:

```powershell
.\scripts\build_esp32.ps1
```

Encode a 320x240 MJPEG/AVI file:

```powershell
.\scripts\encode_mjpeg.ps1 `
    -InputFile .\out\sources\input.mp4 `
    -OutputFile .\out\input_320x240_mjpeg.avi `
    -Width 320 -Height 240 -ResizeMode Crop -Quality 5
```

Use the project-local Python and its pinned packages for Python utilities:

```powershell
.\scripts\python.ps1 .\scripts\benchmark.py --help
```

Generated tool installations belong under `local_tools/`, build products under
`build/`, encoded media under `out/`, and benchmark results under
`bench/results/`.

## Environment and tool wrappers

| Script | Purpose |
| --- | --- |
| `bootstrap.ps1` | Compatibility entry point that runs the complete `setup.ps1` workflow. |
| `setup.ps1` | Prepares MSVC, portable Python, FFmpeg, and the repository-local ESP-IDF environment. Use `-ForceDownload` to refresh downloaded tools or `-SkipVisualStudioInstall` when MSVC is managed separately. |
| `setup_msvc.ps1` | Locates Visual Studio C++ Build Tools and optionally installs the required workload with `winget`. |
| `setup_python.ps1` | Installs the verified portable Python distribution and packages from `requirements-tools.txt` into `local_tools/python`. |
| `bootstrap_ffmpeg.ps1` | Downloads and verifies the project FFmpeg/FFprobe build in `local_tools/ffmpeg`; it also checks the codecs and filters required by the encoding scripts. |
| `bootstrap_arduino.ps1` | Installs the repository-local Arduino CLI and ESP32 Arduino packages used by the remaining Arduino/LittleFS utilities. Normal Player firmware builds use ESP-IDF instead. |
| `python.ps1` | Runs the portable Python with project FFmpeg and MSVC tools added to `PATH`. Remaining arguments are passed directly to Python. |
| `arduino.ps1` | Runs the repository-local `arduino-cli` with `arduino-cli.yaml`. It does not install the CLI automatically. |

## Build scripts

| Script | Purpose |
| --- | --- |
| `build_msvc.ps1` | Builds the HLV command-line tools and tests (`hlvenc`, `hlvdec`, `hlvinfo`, decoder benchmarks, and error/round-trip tests), then builds the Windows Player. |
| `build_windows_player.ps1` | Builds `hlvplay.exe` with the HLV, BPV, MPEG-1, H.263, MPEG-4 SP, DivX 3, AMR-NB, and compact-frame-buffer decoders. |
| `build_bpv_msvc.ps1` | Builds the native BPV encoder and decoder test, runs the decoder test, and runs the JavaScript/C encoder compatibility test when Node.js is available. |
| `build_esp32.ps1` | Builds `firmware/esp32_2432s028_hlv_player_idf_c` with the pinned pure ESP-IDF environment. Pass `-Clean` for a clean build. |
| `build_littlefs.ps1` | Creates the legacy ESP32 LittleFS image from one HLV file. The current SD-card Player normally uses `copy_video_to_sd.ps1` instead. |

## General-purpose encoders

These scripts validate their output with FFprobe and normally create a JSON
report next to the encoded file. `MaxFrames=0` means encode the complete input.

| Script | Purpose and important options |
| --- | --- |
| `prepare_esp32_video.ps1` | Produces an HLV file for ESP32 with configurable dimensions, frame rate, quality, duration, and 16 kHz audio normalization. Without `-InputFile`, it generates a deterministic test clip. |
| `encode_mjpeg.ps1` | Encodes baseline YUV420 MJPEG in AVI with PCM_U8 mono 16 kHz audio. Controls include `Width`, `Height`, `ResizeMode`, `Quality`, `Threads`, and `MaxFrames`. |
| `encode_mpeg1.ps1` | Encodes constrained MPEG-1 Program Stream with no B pictures and MP2 mono 32 kHz audio. Controls include dimensions, `VideoQuality`, GOP, audio bitrate, and frame limit. |
| `encode_h263_avi.ps1` | Encodes baseline H.263 only at standard QCIF `176x144` or CIF `352x288`, always in AVI and at the full source frame rate, with optional PCM S16LE mono audio. Constant-quality Q6 is the default; use `VideoQuality=1..31` to override it or pass zero for bitrate control. |
| `encode_mpeg4_simple_avi.ps1` | Encodes bounded MPEG-4 Part 2 Simple Profile at `320x240` in M4S2 AVI, with I/P pictures only, full source rate up to 30 fps, and optional PCM S16LE mono 8 kHz audio. The default Q5 profile is the accepted `35dB` quality profile. `-Preset Esp32Speed` is an explicit lower-quality decoder-speed trade-off that uses zero/integer motion vectors and stronger coefficient elimination. |
| `analyze_mpeg4_simple.ps1` | Completely decodes an MPEG-4 SP AVI with a profiling Windows build and reports skip/CBP-zero rates, residual sparsity, motion-vector interpolation classes, and average I/P packet sizes as JSON. |
| `encode_bpv.ps1` | Encodes BPV with native frame rate and PCM_U8 mono 16 kHz audio. CUDA is the default backend; use `-Device Auto` or `-Device Cpu` for fallback. It exposes dimensions, GOP, lambda, palette search controls, active/fixed palettes, threads, frame limit, and opt-in BPV v7 `-PixelMotion`. |
| `encode_bpv_target_quality.ps1` | Encodes one or more videos to BPV v6 with CUDA by default and searches lambda independently for the requested RGB PSNR. `-PixelMotion` emits v7. It supports an explicit FPS override and writes per-video reports plus a combined JSON summary. |
| `encode_bpv_from_yaml.ps1` | Reads `out/source/bpv-transcode.yaml` (or `-ConfigFile`) and runs every BPV v6/v7 profile with its own source, resolution, FPS, format and target quality. Select v7 with `codec: BPVv7` or `pixelMotion: true`. It inherits the default CUDA backend. |

The production defaults for every format accepted by the ESP32 player are
stored in `out/source/esp32-transcode.yaml`. It records source preparation,
quality mode, rate control, audio profile and decoder limits in one place.
HLV uses adaptive 35–42 dB quality, H.263 uses constant-quality Q6, and
the remaining codecs target 40 dB where that quality is attainable. DivX 3
always uses half of the source frame rate (12 fps for Bunny, 15 fps for
Danila).

Generated videos and their sidecar reports are grouped under `out` by codec:
`HLV`, `BPV`, `H263`, `MPEG4SP`, `DivX3`, `MJPEG`, and `MPEG1`. Input media and
transcoding configuration remain in `out/sources` and `out/source`.

## Production profile wrappers

Use the `transcode_*.ps1` wrappers for ordinary one-video conversions. They
select the production parameters and output directory automatically:

| Wrapper | Fixed production rules |
| --- | --- |
| `transcode_hlv14.ps1` | Stable syntax v14, slow preset, adaptive 35–42 dB, five CQ trials, GOP 45, PCM_U8 mono 16 kHz. |
| `transcode_bpv6.ps1` | Stable BPV v6 with CUDA by default, source FPS, target RGB PSNR 40 dB, lambda 0–4096, active GOP palettes, GOP 48. `-PixelMotion` opts into experimental BPV v7. |
| `transcode_h263.ps1` | CIF/AVI only, macroblock-aligned visible 320x240 area at `(16,16)`, constant-quality Q6, full source FPS, intra-only. |
| `transcode_mpeg4_simple.ps1` | `320x240` M4S2 AVI, MPEG-4 Simple Profile, GOP 30, I/P pictures only, full source FPS up to 30. The default accepted 35 dB profile uses Q5 and produces a `_MPEG4SP_35dB` filename; `-Preset Esp32Speed` is separately named, defaults to Q7, and produces `_MPEG4SP_SPEED_q7`. |
| `transcode_divx3.ps1` | DIV3 AVI, exactly half source FPS, one-second GOP, no B pictures, maximum packet 98304 bytes. |
| `transcode_mjpeg.ps1` | Baseline MJPEG/AVI with YUVJ420P and PCM_U8 mono 16 kHz. |
| `transcode_mpeg1.ps1` | MPEG-1 Program Stream, GOP 30, no B pictures, 2048-byte packets and MP2 mono 32 kHz. |

Every audio-enabled production wrapper uses the same peak-safe normalization:
convert to the profile's mono sample rate, apply the primary gentle compressor,
measure the complete processed source, and set compressor makeup so the
pre-encode peak reaches -0.1 dBFS. HLV, BPV, DivX 3 and MJPEG use 16 kHz;
H.263 and MPEG-4 Simple Profile use 8 kHz; MPEG-1 uses 32 kHz. Inputs without
an audio stream remain video-only, and `-NoAudio` continues to bypass audio
processing where that option is supported.

All wrappers refuse to overwrite an existing video unless `-Force` is
specified. Width and height default to 320x240; use 320x180 for the 16:9
variant. `ResizeMode=Auto` crops the 320x240 variant and stretches 320x180.
Big Buck Bunny is always restricted to the approved repository 1080p MOV.

Examples:

```powershell
.\scripts\transcode_h263.ps1 .\out\sources\input.mp4
.\scripts\transcode_mpeg4_simple.ps1 .\out\sources\input.mp4
.\scripts\transcode_mpeg4_simple.ps1 .\out\sources\input.mp4 `
    -Preset Esp32Speed
.\scripts\transcode_divx3.ps1 .\out\sources\input.mp4 -Height 180
.\scripts\transcode_hlv14.ps1 .\out\sources\input.mp4 -Height 180
.\scripts\transcode_bpv6.ps1 .\out\sources\input.mp4 `
    -Height 180 -TargetPsnrDb 40
.\scripts\transcode_bpv6.ps1 .\out\sources\input.mp4 `
    -Height 180 -TargetPsnrDb 40 -PixelMotion
```

Example H.263 and BPV calls:

```powershell
.\scripts\encode_h263_avi.ps1 `
    -InputFile .\out\sources\input.mp4 `
    -OutputFile .\out\input_352x288_h263.avi `
    -Profile 352x288 -FitMode Crop

.\scripts\encode_bpv.ps1 `
    -InputFile .\out\sources\input.mp4 `
    -OutputFile .\out\input_320x240.bpv `
    -Width 320 -Height 240 -ResizeMode Crop -Lambda 64

.\scripts\encode_bpv_target_quality.ps1 `
    -InputFile @(
        ".\out\sources\video-one.mp4",
        ".\out\sources\video-two.mp4"
    ) `
    -OutputName @("VideoOne", "VideoTwo") `
    -TargetPsnrDb 35 `
    -Width 320 -Height 240 -ResizeMode Crop

.\scripts\encode_bpv_from_yaml.ps1 `
    -ConfigFile .\out\source\bpv-transcode.yaml
```

The target-quality script prepares each input only once and searches `lambda`
against the native encoder's measured RGB PSNR. `MaxFrames=0` searches and
encodes the complete videos; a positive value deliberately creates leading
test fragments. The output name contains the measured rounded quality, while
the adjacent JSON report records the requested value, tolerance, selected
lambda and every search trial. A zero-error result is marked `lossless`
instead of being misreported as 0 dB. If a Big Buck Bunny input is detected,
the script accepts only the approved 1080p MOV listed below.

The YAML runner uses a strict dependency-free subset of YAML: scalar
top-level settings and a flat `videos` list. Relative input, output and summary
paths are resolved from the directory containing the YAML file. The supplied
configuration contains separate `320x180` and cropped `320x240` Danila
profiles at 30 fps. A profile may use `codec: BPVv7` or
`pixelMotion: true` to enable pixel motion. Command-line `-MaxFrames` and
`-NoAudio` are useful for
short validation runs without changing the saved profiles. Use
`-ValidateOnly` to check the schema, values and input paths without encoding.

## Reproducible source-specific profiles

For every Big Buck Bunny transcode, the authoritative source is:

```text
out/sources/big_buck_bunny_1080p_h264/big_buck_bunny_1080p_h264.mov
```

Do not substitute the 320x180 download or another reduced copy.

| Script | Purpose |
| --- | --- |
| `encode_big_buck_bunny_v13.ps1` | Encodes the approved 1080p MOV to HLV stream version 13 at 320x180, with optional frame-rate override, threading, frame limit, and SIMD disable switch. |
| `encode_big_buck_bunny_mjpeg.ps1` | Encodes the approved MOV to the native-frame-rate 320x180 MJPEG/AVI profile and updates `out/play.txt` unless another selection file is supplied. |
| `encode_big_buck_bunny_divx3.ps1` | Encodes the approved MOV to the 320x240 DivX 3 AVI profile with configurable quality, FPS, GOP, and frame limit; also updates the selection file. |
| `encode_big_buck_bunny_h263_avi.ps1` | Encodes the approved MOV as baseline H.263 in CIF or QCIF AVI at the full source frame rate. |
| `encode_big_buck_bunny_bpv.ps1` | Applies the 320x180 BPV profile to the approved MOV and writes its JSON report. |
| `encode_vid_20260522_181611_mpeg1.ps1` | Reproducible 240x180 MPEG-1/MP2 preset for `out/sources/VID_20260522_181611.mp4`. |

## ESP32 deployment and media transfer

| Script | Purpose |
| --- | --- |
| `upload_esp32.ps1` | Builds and flashes the pure ESP-IDF Player through the specified serial port. `-SkipBuild` flashes the existing build and `-Baud` selects the upload rate. |
| `upload_video_uart.ps1` | Uploads one media file through the Player's UART file-transfer protocol. `-Name` overrides the destination name and `-DataBaud` controls the high-speed data phase. |
| `copy_video_to_sd.ps1` | Copies a selected video to the mounted SD card, verifies it with SHA-256, and writes `HLV/play.txt` so the Player selects it at boot. |

Examples:

```powershell
.\scripts\upload_esp32.ps1 -Port COM8
.\scripts\upload_video_uart.ps1 -Port COM8 -File .\out\video.hlv
.\scripts\copy_video_to_sd.ps1 -DestinationRoot E:\ -InputFile .\out\video.hlv
```

## Regression and smoke tests

| Script | Purpose |
| --- | --- |
| `test_compact_yuv420.ps1` | Builds and runs the native packed frame-buffer tests, including Y6/U5/V5 fast paths, HLV Y7 fallback unpacking, and correction maps. |
| `test_all_video_formats.ps1` | Generates a short deterministic, picture-rich source with motion and audio; encodes production-named HLV, BPV, MJPEG, DivX 3, MPEG-1, baseline H.263, and MPEG-4 SP clips; then requires complete host decoding and writes a manifest for QEMU/physical acceptance. |
| `test_divx3.ps1` | Builds the DivX 3 regression decoder, creates a deterministic 256x144 sample from the approved MOV, and verifies pixel-exact output against FFmpeg, including AVI with ignored MP3 audio. |
| `compare_divx3_compact.ps1` | Builds and runs a frame-by-frame comparison of exact and compact DivX 3 decoder storage for a supplied AVI. |
| `test_mpeg1_compact.ps1` | Builds exact and compact MPEG-1 decoder variants and verifies that a supplied MPEG stream produces matching frame counts and checksums. |
| `test_h263_avi.ps1` | Generates a 30 fps synthetic source and verifies standard QCIF/CIF H.263 AVI encoding and decoding without frame-rate reduction. |
| `test_mpeg4_simple.ps1` | Verifies MPEG-4 SP/M4S2 encoding and compares decoded checksums for a video packet larger than 4 KiB through fixed-refill and contiguous-input builds. |
| `generate_all_video_formats.ps1` | Creates the deterministic picture-rich regression source once, then writes production HLV, BPV, H.263, MPEG-4 SP, DivX 3, MJPEG, and MPEG-1 versions into their matching directories under `out`. |
| `test_transcode_wrappers.ps1` | Encodes all seven production formats with audio, verifies the normalized codec/rate/channel profiles and measurable peaks for FFmpeg-readable containers, and checks HLV/BPV reports. |
| `test_threaded_encode.py` | Verifies that parallel HLV GOP encoding is enabled by default and remains byte-exact against the serial encoder. |
| `test_windowed_two_pass.py` | Smoke-tests bounded local two-pass HLV rate control through an FFmpeg Y4M pipe. |

## Benchmarks, comparisons, and data sets

| Script | Purpose |
| --- | --- |
| `benchmark.py` | Runs reproducible rate-distortion benchmarks for HLV, BPV, MJPEG, MPEG-1/2, H.264, VP8/9, and optionally AV1. It records bitrate, quality, encoding speed, and decoding speed. |
| `matched_bitrate.py` | Searches codec settings to match requested measured bitrates and compares PSNR/SSIM at those rates. Supports resumable HLV, BPV, and reference-codec runs. |
| `benchmark_encoder.py` | Measures exact HLV encoder variants and reports algorithmic work and throughput over repeated runs. |
| `compare_hlv_versions.py` | Compares two HLV syntax or encoder configurations on identical source frames and target bitrates. |
| `summarize_results.py` | Converts benchmark JSON into compact per-source and aggregate Markdown plus CSV reports. |
| `make_local_suite.py` | Generates deterministic synthetic and UI-style benchmark clips without network access. |
| `fetch_open_sources.py` | Downloads the explicitly listed openly licensed sources for the extended benchmark suite. |
| `fetch_big_buck_bunny.ps1` | Downloads and verifies the small 320x180 Big Buck Bunny fixture. It is for tests only and must not be used as a Big Buck Bunny transcoding source. |

Typical benchmark workflow:

```powershell
.\scripts\python.ps1 .\scripts\make_local_suite.py --duration 60 --fps 15
.\scripts\python.ps1 .\scripts\benchmark.py `
    --sources .\bench\sources\*.mp4 `
    --duration 10 --fps 15 --prefix local
.\scripts\python.ps1 .\scripts\summarize_results.py `
    .\bench\results\local.json
```

## Reverse-engineering helpers

| Script | Purpose |
| --- | --- |
| `ghidra/ExportAnnotatedDecompilation.java` | Ghidra post-script that exports every retained function in the current program as byte-accurate annotated disassembly and matching pseudo-C. It adds a short semantic `Purpose` comment for each known ESP_NEW_JPEG decoder function. |

The ESP_NEW_JPEG 1.0.2 decoder results and the exact headless command are
documented in
[`docs/reverse_engineering/esp_new_jpeg_1.0.2`](../docs/reverse_engineering/esp_new_jpeg_1.0.2/README.md).
Keep Ghidra itself under `local_tools/ghidra` and Ghidra projects under `out/`;
neither installation output belongs in Git.
