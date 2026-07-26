#requires -Version 7.4

<#
.SYNOPSIS
Creates high-quality 320-pixel source variants with anti-aliasing.

.DESCRIPTION
Produces two Windows Media Player-compatible MP4 files from a 16:9 source:

- 320x180, preserving the complete 16:9 frame.
- 320x240, using a centered 4:3 crop before scaling.

Both variants apply a Gaussian low-pass filter before AREA downscaling to
reduce shimmer on fine textures. Video is encoded as H.264 High, YUV420P,
with constant QP 1. The first audio stream is copied without re-encoding.

.EXAMPLE
./scripts/encode_smoothed_source_variants.ps1 `
    -InputFile ./out/sources/VID_20260522_181611.mp4

.EXAMPLE
./scripts/encode_smoothed_source_variants.ps1 `
    -InputFile ./input.mp4 `
    -OutputDirectory ./out/sources `
    -Sigma 1.0 `
    -Qp 1 `
    -Force
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [string]$OutputDirectory,

    [ValidateRange(0.1, 10.0)]
    [double]$Sigma = 1.0,

    [ValidateRange(1, 51)]
    [int]$Qp = 1,

    [ValidateSet(
        "ultrafast",
        "superfast",
        "veryfast",
        "faster",
        "fast",
        "medium",
        "slow",
        "slower",
        "veryslow"
    )]
    [string]$Preset = "veryslow",

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"

$InputFile = [IO.Path]::GetFullPath($InputFile)
if (-not (Test-Path -LiteralPath $InputFile -PathType Leaf)) {
    throw "Input video is missing: $InputFile"
}
if (-not $OutputDirectory) {
    $OutputDirectory = Split-Path $InputFile -Parent
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    throw "Repository-local FFmpeg is unavailable."
}

$probeOutput = & $ffprobe -v error -select_streams v:0 `
    -show_entries stream=width,height -of json $InputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe could not inspect the input video."
}
$sourceStream = ($probeOutput | ConvertFrom-Json).streams | Select-Object -First 1
if (-not $sourceStream) {
    throw "The input file does not contain a video stream."
}
$sourceAspect = [double]$sourceStream.width / [double]$sourceStream.height
$expectedAspect = 16.0 / 9.0
if ([Math]::Abs($sourceAspect - $expectedAspect) -gt 0.01) {
    throw (
        "The source must be 16:9, but it is {0}x{1}." -f
        $sourceStream.width, $sourceStream.height
    )
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$culture = [Globalization.CultureInfo]::InvariantCulture
$sigmaText = $Sigma.ToString("0.###", $culture)
$baseName = [IO.Path]::GetFileNameWithoutExtension($InputFile)
$suffix = "area_sigma${sigmaText}_qp${Qp}"
$output16x9 = Join-Path $OutputDirectory (
    "${baseName}_320x180_${suffix}.mp4"
)
$output4x3 = Join-Path $OutputDirectory (
    "${baseName}_crop_320x240_${suffix}.mp4"
)

if (-not $Force) {
    foreach ($output in $output16x9, $output4x3) {
        if (Test-Path -LiteralPath $output) {
            throw "Output already exists; use -Force to replace it: $output"
        }
    }
}

$scaleFlags = "area+accurate_rnd+full_chroma_int"
$filter = (
    "[0:v]split=2[v16][v43];" +
    "[v16]gblur=sigma=${sigmaText}:steps=2," +
    "scale=320:180:flags=${scaleFlags}," +
    "setsar=1,format=yuv420p[out16];" +
    "[v43]crop=ih*4/3:ih:(iw-ih*4/3)/2:0," +
    "gblur=sigma=${sigmaText}:steps=2," +
    "scale=320:240:flags=${scaleFlags}," +
    "setsar=1,format=yuv420p[out43]"
)
$overwriteFlag = if ($Force) { "-y" } else { "-n" }
$arguments = @(
    $overwriteFlag,
    "-hide_banner",
    "-loglevel", "warning",
    "-stats",
    "-i", $InputFile,
    "-filter_complex", $filter,
    "-map", "[out16]",
    "-map", "0:a:0?",
    "-c:v", "libx264",
    "-preset", $Preset,
    "-qp", $Qp,
    "-profile:v", "high",
    "-pix_fmt", "yuv420p",
    "-tag:v", "avc1",
    "-c:a", "copy",
    "-movflags", "+faststart",
    $output16x9,
    "-map", "[out43]",
    "-map", "0:a:0?",
    "-c:v", "libx264",
    "-preset", $Preset,
    "-qp", $Qp,
    "-profile:v", "high",
    "-pix_fmt", "yuv420p",
    "-tag:v", "avc1",
    "-c:a", "copy",
    "-movflags", "+faststart",
    $output4x3
)

Write-Host (
    "Encoding 320x180 and center-cropped 320x240 variants: " +
    "Gaussian sigma $sigmaText, AREA, H.264 High QP $Qp..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg encoding failed with exit code $LASTEXITCODE."
}

$expectedDimensions = @{
    $output16x9 = @(320, 180)
    $output4x3 = @(320, 240)
}
foreach ($output in $output16x9, $output4x3) {
    Write-Host "Fully decoding: $output"
    & $ffmpeg -v error -i $output -map 0:v:0 -map "0:a:0?" -f null NUL
    if ($LASTEXITCODE -ne 0) {
        throw "Full decode validation failed: $output"
    }

    $outputProbe = & $ffprobe -v error -select_streams v:0 `
        -show_entries stream=codec_name,profile,pix_fmt,width,height `
        -of json $output
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe validation failed: $output"
    }
    $stream = ($outputProbe | ConvertFrom-Json).streams |
        Select-Object -First 1
    $dimensions = $expectedDimensions[$output]
    if ($stream.codec_name -ne "h264" -or
        $stream.profile -ne "High" -or
        $stream.pix_fmt -ne "yuv420p" -or
        $stream.width -ne $dimensions[0] -or
        $stream.height -ne $dimensions[1]) {
        throw "Unexpected output video profile: $output"
    }
}

foreach ($output in $output16x9, $output4x3) {
    $result = Get-Item -LiteralPath $output
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f
        $result.FullName, $result.Length)
}
