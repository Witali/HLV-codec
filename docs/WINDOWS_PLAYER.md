# Native Windows HLV/BPV/MPEG-1/H.263 player

`hlvplay.exe` is a dependency-free Windows desktop player for HLV-1, BPV1
v1 through v5, the constrained MPEG-1/MP2 profile, and the
bounded H.263 profiles in 3GP with AMR-NB or AVI with PCM S16LE at `176x144`
and intra-only H.263+ profiles at `256x144`, `256x192`, `320x180`, and
`320x240`. It uses the portable
codec sources in this repository, a D3D11
video processor and a two-buffer DXGI flip swap chain for presentation, and
the Windows `waveOut` API for unsigned 8-bit mono PCM. MPEG-1 Audio Layer II is
decoded and downmixed into that output format. AMR-NB is decoded from 8 kHz
mono PCM16 into the same unsigned PCM8 output. HLV/MPEG YUV and decoded BPV
palette blocks are prepared as NV12 plus BGRA; AVI PCM S16LE is converted
directly to the same PCM8 output. The GPU performs colour conversion and
scaling. If D3D11 initialisation or presentation fails, the
player automatically switches to double-buffered GDI. BPV1 v4/v5 supports
active per-GOP palettes and PCM_U8 audio; video-only files use the
high-resolution frame timer without opening audio. BPV1 v5 additionally uses adaptive
2/4/7-byte RAW records and packed local palette indices.
No FFmpeg, codec pack or third-party runtime DLL is required.

## Setup and build

Install or verify the Visual Studio C++ workload with the repository setup:

```powershell
.\setup.ps1
```

Build only the player:

```powershell
.\scripts\build_windows_player.ps1
```

The executable is written to `build\msvc\hlvplay.exe`. The ordinary desktop
build also includes it:

```powershell
.\scripts\build_msvc.ps1
```

## Playback

Open a file from PowerShell:

```powershell
.\build\msvc\hlvplay.exe .\out\video.hlv
.\build\msvc\hlvplay.exe .\out\video.bpv1
.\build\msvc\hlvplay.exe .\out\video.mpg
.\build\msvc\hlvplay.exe .\out\video.3gp
.\build\msvc\hlvplay.exe .\out\video.avi
```

The player also accepts files through **File > Open** and drag-and-drop.

- Drag the timeline at the bottom of the window to choose the playback time.
- With the timeline focused, `Left`/`Right` seek by one second,
  `Page Up`/`Page Down` by ten seconds, and `Home`/`End` select the boundaries.
- `Space`: pause or resume video and audio together.
- `F`: switch between aspect-preserving fit and native-size centred display.
- `Ctrl+O`: open another file.
- `Esc`: close the player.

The time label beside the timeline shows the selected position and total
duration. Four video/audio intervals are prepared at startup. Video timing uses
the HLV frame-rate fraction and a high-resolution Windows clock. Audio is
queued in eight reusable `waveOut` buffers. The image remains centred and its
aspect ratio is never distorted.

When a file is opened, the player validates its packet layout and builds a
compact frame-to-keyframe index. A seek starts at the nearest preceding
keyframe, decodes dependent frames without presenting them, then displays the
selected frame. HLV/BPV audio restarts from the matching packet. MPEG seeking
reopens its bounded decoder and decodes video and MP2 forward from the start to
keep both streams aligned. H.263 seeking likewise reopens the bounded 3GP or
AVI readers and decodes H.263 plus AMR-NB/PCM forward from the start.
Seeking while paused preserves the paused state. HLV files whose header has a
streaming `frame_count=0` remain seekable because their local index uses the
actual packet count. BPV indexing also rejects truncated frames and bytes
following the declared frame count.

The window title reports the active presentation path:

- `D3D11 NV12 flip`: NV12 input, GPU colour conversion and a two-buffer flip
  swap chain.
- `D3D11 NV12 flip, MPO-capable`: the active output and adapter also report
  NV12 hardware-overlay support. Windows decides dynamically whether a frame
  is promoted to a multiplane overlay.
- `double-buffered GDI`: automatic compatibility fallback.

## File verification

The headless check path decodes every packet and prepares every frame through
the same NV12/BGRA conversion routines used by the window. For HLV it also
validates every audio tail. Its deterministic checksum is calculated from the
BGRA result:

```powershell
.\build\msvc\hlvplay.exe --check .\out\video.hlv
.\build\msvc\hlvplay.exe --check .\out\video.bpv1
.\build\msvc\hlvplay.exe --check .\out\video.mpg
.\build\msvc\hlvplay.exe --check .\out\video.3gp
if ($LASTEXITCODE -ne 0) { throw "Video validation failed" }
```

The headless check still performs a complete independent sequential decode;
the interactive seek index does not bypass packet CRC validation.
