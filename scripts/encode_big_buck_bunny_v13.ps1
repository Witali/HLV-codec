#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateRange(1, 30)]
    [int]$Fps = 24,

    [ValidateRange(1, 8)]
    [int]$Threads = 4,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$DisableSimd,

    [string]$OutputFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"

if (-not $OutputFile) {
    $OutputFile = Join-Path $repo `
        ("out\BigBuckBunny_1080p_video-settings_v13_{0}fps_normalized.hlv" -f $Fps)
}
if (-not [IO.Path]::IsPathRooted($OutputFile)) {
    $OutputFile = [IO.Path]::GetFullPath($OutputFile)
}

if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p source is missing: $source"
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $encoder)) {
    & (Join-Path $PSScriptRoot "build_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $encoder)) {
    throw "FFmpeg or hlvenc is unavailable."
}

$outputParent = Split-Path $OutputFile -Parent
if ($outputParent) {
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
}

$temporaryAudio = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv-bbb-v13-{0}.u8" -f [guid]::NewGuid().ToString("N"))
$audioFilter = "acompressor=threshold=-18dB:ratio=2.5:" +
    "attack=20:release=250:makeup=2dB,volume=1"
$videoFilter = "fps=$Fps," +
    "scale=320:180:force_original_aspect_ratio=decrease:flags=lanczos," +
    "pad=320:180:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p"

try {
    Write-Host "Preparing normalized PCM from the 1080p MOV source..."
    & $ffmpeg -y -hide_banner -loglevel error -i $source `
        -map 0:a:0 -vn -af $audioFilter `
        -ac 1 -ar 16000 -f u8 $temporaryAudio
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg audio conversion failed with exit code $LASTEXITCODE."
    }

    $ffmpegArguments = @(
        "-y", "-hide_banner", "-loglevel", "error",
        "-i", $source, "-an", "-vf", $videoFilter
    )
    if ($MaxFrames) {
        $ffmpegArguments += @("-frames:v", $MaxFrames)
    }
    $ffmpegArguments += @("-f", "yuv4mpegpipe", "-")

    $encoderArguments = @(
        "-", $OutputFile,
        "--preset", "balanced",
        "--quality", 45,
        "--gop", 30,
        "--syntax", 13,
        "--threads", $Threads,
        "--audio-u8", $temporaryAudio,
        "--audio-rate", 16000
    )
    if ($MaxFrames) {
        $encoderArguments += @("--max-frames", $MaxFrames)
    }
    if ($DisableSimd) {
        $encoderArguments += @("--simd", "off")
    }

    $profileMessage = (
        "Encoding 320x180 at {0} fps, HLV v13, quality 45, " +
        "GOP 30, threads {1}..."
    ) -f $Fps, $Threads
    Write-Host $profileMessage
    & $ffmpeg @ffmpegArguments | & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "HLV encoding failed with exit code $LASTEXITCODE."
    }

    $result = Get-Item -LiteralPath $OutputFile
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f
        $result.FullName, $result.Length)
} finally {
    if (Test-Path -LiteralPath $temporaryAudio) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
