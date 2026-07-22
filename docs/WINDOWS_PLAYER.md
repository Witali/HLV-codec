# Native Windows HLV player

`hlvplay.exe` is a dependency-free Windows desktop player for HLV-1 files. It
uses the codec sources in this repository, GDI for BGRA video presentation and
the Windows `waveOut` API for unsigned 8-bit mono PCM. No FFmpeg, codec pack or
third-party runtime DLL is required for playback.

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

## File verification

The headless check path decodes every packet, validates its audio tail and
converts all frames through the same YUV-to-BGRA routine used by the window:

```powershell
.\build\msvc\hlvplay.exe --check .\out\video.hlv
if ($LASTEXITCODE -ne 0) { throw "HLV validation failed" }
```

Seeking is not implemented yet because P-frames depend on previous reference
frames. A future seek index should point to keyframes and resume decoding from
the selected keyframe.
