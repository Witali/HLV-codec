# Native Windows HLV/BPV/MPEG-1/H.263 player

`hlvplay.exe` is a dependency-free Windows desktop player for stable HLV v14,
BPV1 v1 through v7, the constrained MPEG-1/MP2 profile, and the
bounded H.263 profiles in 3GP with AMR-NB or AVI with PCM S16LE at `176x144`,
intra-only baseline `352x288` CIF, and intra-only H.263+ profiles at
`256x144`, `256x192`, `320x180`, and `320x240`. CIF playback retains the
complete square-pixel `352x288` bordered frame. It uses the portable
codec sources in this repository, a D3D11
video processor and a two-buffer DXGI flip swap chain for presentation, and
the Windows `waveOut` API for unsigned 8-bit mono PCM. MPEG-1 Audio Layer II is
decoded and downmixed into that output format. AMR-NB is decoded from 8 kHz
mono PCM16 into the same unsigned PCM8 output. HLV/MPEG/H.263 YUV is sent to
D3D11 as NV12. BPV is sent directly as BGRA, preserving the RGB888 palette
output of v1-v6 and the normative RGB565 output of v7 without an intermediate
4:2:0 chroma conversion. AVI PCM S16LE is converted directly to the same PCM8
output. The GPU performs colour conversion where needed and scales every
format. If D3D11 initialisation or presentation fails, the player automatically
switches to double-buffered GDI. BPV1 v4-v7 supports
active per-GOP palettes and PCM_U8 audio; video-only files use the
high-resolution frame timer without opening audio. BPV1 v6 uses four 2-bit
block modes, one-byte motion vectors and unified 2/4/7/9-byte RAW records.
BPV1 v7 additionally decodes pixel-motion frames and RGB565 palettes; its
dimensions must be multiples of four. Standalone HLV files from versions
1-13 are intentionally rejected because HLV v14 is the current stable format.
No FFmpeg, codec pack or third-party runtime DLL is required.

The video output has its own child window above the timeline. The DXGI swap
chain is bound only to that child window, while the seek bar and time label are
separate sibling controls below it. This prevents a promoted hardware overlay
from covering the controls even though they remain responsive. The GDI
fallback draws into the same video child window and therefore uses identical
layout boundaries.

New project H.263 assets are restricted to baseline H.263 in AVI at standard
QCIF `176x144` or CIF `352x288`, preserving the full source frame rate. The
encoder prepares a complete 4:3 QCIF/CIF frame with one Lanczos downscale.
Legacy 3GP and custom-size H.263+ support above is playback compatibility only.
The Windows Player displays the complete CIF frame, while the ESP32 crops its
central `320x240` area for the panel.

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

- `D3D11 BGRA flip`: direct full-colour BPV input and a two-buffer flip swap
  chain, without RGB-to-NV12 conversion.
- `D3D11 NV12 flip`: HLV/MPEG/H.263 NV12 input, GPU colour conversion and a
  two-buffer flip swap chain.
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
