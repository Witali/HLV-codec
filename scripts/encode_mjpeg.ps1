#requires -Version 7.4

# Encode the standard MJPEG/AVI profile supported by both project players.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [Parameter(Mandatory)]
    [string]$OutputFile,

    [ValidateRange(16, 320)]
    [int]$Width = 320,

    [ValidateRange(16, 240)]
    [int]$Height = 240,

    [ValidateSet("Stretch", "Crop")]
    [string]$ResizeMode = "Crop",

    [ValidateRange(2, 31)]
    [int]$Quality = 5,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"

$InputFile = [IO.Path]::GetFullPath($InputFile)
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
$ReportFile = [IO.Path]::GetFullPath($ReportFile)

if (($Width -band 1) -or ($Height -band 1)) {
    throw "MJPEG YUV420 dimensions must be even: ${Width}x${Height}."
}
if (-not (Test-Path -LiteralPath $InputFile)) {
    throw "Input video is missing: $InputFile"
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    throw "Repository-local FFmpeg is unavailable."
}

New-Item -ItemType Directory -Force -Path (
    Split-Path $OutputFile -Parent
) | Out-Null
New-Item -ItemType Directory -Force -Path (
    Split-Path $ReportFile -Parent
) | Out-Null

# Keep this curve in sync with encode_bpv.ps1 and encode_mpeg1.ps1.
# There is no EQ, loudness filter, standalone volume stage or limiter.
$audioConversion = "aformat=channel_layouts=mono,aresample=16000"
$audioLevelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
    "attack=0.01:release=250:knee=8:" +
    "link=maximum:detection=peak"
$audioPeakTargetDb = -0.1

Write-Host "Measuring the primary audio level curve..."
$analysisFilter = (
    "$audioConversion,$audioLevelCurve," +
    "astats=metadata=0:reset=0"
)
$analysisOutput = & $ffmpeg -hide_banner -nostats -i $InputFile `
    -map 0:a:0 -vn -af $analysisFilter `
    -ac 1 -ar 16000 -f null NUL 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg audio analysis failed with exit code $LASTEXITCODE."
}
$peakMatches = [regex]::Matches(
    ($analysisOutput -join "`n"),
    "Peak level dB:\s*(-?\d+(?:\.\d+)?)"
)
if (-not $peakMatches.Count) {
    throw "FFmpeg did not report the processed audio peak."
}
$culture = [Globalization.CultureInfo]::InvariantCulture
$curvePeakDb = (
    $peakMatches |
        ForEach-Object {
            [double]::Parse($_.Groups[1].Value, $culture)
        } |
        Measure-Object -Maximum
).Maximum
$curveMakeupDb = $audioPeakTargetDb - $curvePeakDb
if ($curveMakeupDb -lt 0.0) {
    throw "The primary audio curve would require attenuation."
}
$curveMakeupText = $curveMakeupDb.ToString("0.000", $culture)
$audioFilter = (
    "$audioConversion,${audioLevelCurve}:" +
    "makeup=${curveMakeupText}dB"
)

if ($ResizeMode -eq "Crop") {
    $videoFilter = (
        "scale=${Width}:${Height}:force_original_aspect_ratio=increase:" +
        "force_divisible_by=2:flags=lanczos," +
        "crop=${Width}:${Height}:(iw-${Width})/2:(ih-${Height})/2," +
        "setsar=1,format=yuvj420p"
    )
} else {
    $videoFilter = (
        "scale=${Width}:${Height}:flags=lanczos," +
        "setsar=1,format=yuvj420p"
    )
}

$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $InputFile,
    "-map", "0:v:0", "-map", "0:a:0",
    "-vf", $videoFilter,
    "-af", $audioFilter,
    "-fps_mode", "cfr",
    "-c:v", "mjpeg",
    "-q:v", $Quality,
    "-threads:v", $Threads,
    "-pix_fmt", "yuvj420p",
    "-c:a", "pcm_u8",
    "-ar", "16000",
    "-ac", "1",
    "-shortest"
)
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
$arguments += @("-f", "avi", $OutputFile)

Write-Host (
    "Encoding standard MJPEG/AVI: ${Width}x${Height}, native FPS, " +
    "quality $Quality, PCM_U8 mono 16 kHz, $Threads threads..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg MJPEG encoding failed with exit code $LASTEXITCODE."
}

Write-Host "Decoding the complete MJPEG/AVI file with FFmpeg..."
& $ffmpeg -v error -i $OutputFile -map 0:v:0 -map 0:a:0 `
    -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full MJPEG/AVI validation failed."
}

$report = & $ffprobe -v error -count_frames -show_format `
    -show_streams -of json $OutputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe report generation failed."
}
$report | Set-Content -LiteralPath $ReportFile -Encoding utf8

$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host (
    "Audio curve: peak {0:N2} dBFS, makeup {1} dB, target {2:N1} dBFS" -f
    $curvePeakDb, $curveMakeupText, $audioPeakTargetDb
)
Write-Host "Report: $ReportFile"
