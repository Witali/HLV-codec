#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(1, 65535)]
    [int]$Gop = 48,

    [ValidateRange(0, 1000000000)]
    [double]$Lambda = 64,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$OutputFile,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$encoder = Join-Path $repo "build\msvc\bpv1enc.exe"

if (-not $OutputFile) {
    $OutputFile = Join-Path $repo `
        "out\BigBuckBunny_1080p_bpv1_v2_lambda64_native-fps_320x180.bpv1"
}
if (-not [IO.Path]::IsPathRooted($OutputFile)) {
    $OutputFile = [IO.Path]::GetFullPath($OutputFile)
}
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
if (-not [IO.Path]::IsPathRooted($ReportFile)) {
    $ReportFile = [IO.Path]::GetFullPath($ReportFile)
}

if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p source is missing: $source"
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $encoder)) {
    & (Join-Path $PSScriptRoot "build_bpv_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $encoder)) {
    throw "FFmpeg or bpv1enc is unavailable."
}

$outputParent = Split-Path $OutputFile -Parent
$reportParent = Split-Path $ReportFile -Parent
$temporaryDirectory = Join-Path $repo ".tmp"
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
$temporaryVideo = Join-Path $temporaryDirectory `
    ("bpv1-bbb-{0}.y4m" -f [guid]::NewGuid().ToString("N"))

# A 320-pixel width and an automatically calculated even height preserve the
# approved MOV's 16:9 display aspect ratio, producing exactly 320x180.
$videoFilter = "scale=320:-2:flags=lanczos,setsar=1,format=yuv420p"

try {
    $ffmpegArguments = @(
        "-y", "-hide_banner", "-loglevel", "error",
        "-i", $source,
        "-map", "0:v:0",
        "-an",
        "-vf", $videoFilter,
        "-fps_mode", "passthrough"
    )
    if ($MaxFrames) {
        $ffmpegArguments += @("-frames:v", $MaxFrames)
    }
    $ffmpegArguments += @(
        "-pix_fmt", "yuv420p",
        "-f", "yuv4mpegpipe",
        $temporaryVideo
    )

    Write-Host (
        "Preparing BPV1 input from the approved 1080p MOV: " +
        "320x180, native frame rate..."
    )
    & $ffmpeg @ffmpegArguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg video conversion failed with exit code $LASTEXITCODE."
    }

    $encoderArguments = @(
        $temporaryVideo,
        $OutputFile,
        "--threads", $Threads,
        "--gop", $Gop,
        "--lambda", $Lambda,
        "--candidate-palettes", 3,
        "--search-radius", 2,
        "--max-block-dictionary", 256,
        "--max-pattern-dictionary", 256,
        "--sample-blocks", 32768,
        "--samples-per-frame", 16,
        "--report", $ReportFile,
        "--force"
    )

    Write-Host (
        "Encoding BPV1 v2: 320x180, native FPS, lambda $Lambda, " +
        "GOP $Gop, $Threads C worker threads..."
    )
    & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "BPV1 encoding failed with exit code $LASTEXITCODE."
    }

    $result = Get-Item -LiteralPath $OutputFile
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f
        $result.FullName, $result.Length)
    Write-Host "Report: $ReportFile"
}
finally {
    if (Test-Path -LiteralPath $temporaryVideo) {
        Remove-Item -LiteralPath $temporaryVideo -Force
    }
}
