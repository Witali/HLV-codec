# Native Windows HLV player

`hlvplay.exe` is a dependency-free Windows desktop player for HLV-1 files. It
uses the codec sources in this repository, a D3D11 video processor and a
two-buffer DXGI flip swap chain for video presentation, and the Windows
`waveOut` API for unsigned 8-bit mono PCM. Decoded YUV 4:2:0 frames are uploaded
as NV12; the GPU performs colour conversion and scaling. If D3D11
initialisation or presentation fails, the player automatically switches to a
double-buffered GDI path. No FFmpeg, codec pack or third-party runtime DLL is
required for playback.

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
```

The player also accepts files through **File > Open** and drag-and-drop.

- `Space`: pause or resume video and audio together.
- `F`: switch between aspect-preserving fit and native-size centred display.
- `Ctrl+O`: open another file.
- `Esc`: close the player.

Four video/audio intervals are prepared at startup. Video timing uses the HLV
frame-rate fraction and a high-resolution Windows clock. Audio is queued in
eight reusable `waveOut` buffers. The image remains centred and its aspect
ratio is never distorted.

The window title reports the active presentation path:

- `D3D11 NV12 flip`: NV12 input, GPU colour conversion and a two-buffer flip
  swap chain.
- `D3D11 NV12 flip, MPO-capable`: the active output and adapter also report
  NV12 hardware-overlay support. Windows decides dynamically whether a frame
  is promoted to a multiplane overlay.
- `double-buffered GDI`: automatic compatibility fallback.

## File verification

The headless check path decodes every packet, validates its audio tail and
prepares every frame through the same YUV-to-NV12/BGRA conversion routine used
by the window. Its deterministic checksum is calculated from the BGRA result:

```powershell
.\build\msvc\hlvplay.exe --check .\out\video.hlv
if ($LASTEXITCODE -ne 0) { throw "HLV validation failed" }
```

Seeking is not implemented yet because P-frames depend on previous reference
frames. A future seek index should point to keyframes and resume decoding from
the selected keyframe.
