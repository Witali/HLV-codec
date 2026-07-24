# Native Windows HLV/BPV player

`hlvplay.exe` is a dependency-free Windows desktop player for HLV-1 and BPV1
v1/v2 files. It uses the portable codec sources in this repository, a D3D11
video processor and a two-buffer DXGI flip swap chain for presentation, and
the Windows `waveOut` API for HLV unsigned 8-bit mono PCM. HLV YUV and decoded
BPV palette blocks are prepared as NV12 plus BGRA; the GPU performs colour
conversion and scaling. If D3D11 initialisation or presentation fails, the
player automatically switches to double-buffered GDI. BPV1 is video-only, so
`.bpv1` playback uses the high-resolution frame timer without opening audio.
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
selected frame. HLV audio restarts from the matching packet; BPV has no audio.
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
if ($LASTEXITCODE -ne 0) { throw "Video validation failed" }
```

The headless check still performs a complete independent sequential decode;
the interactive seek index does not bypass packet CRC validation.
