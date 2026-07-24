# Encode the constrained MPEG-1/MP2 profile supported by both project players.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [Parameter(Mandatory)]
    [string]$OutputFile,

    [ValidateRange(16, 320)]
    [int]$Width = 240,

    [ValidateRange(16, 240)]
    [int]$Height = 180,

    [ValidateRange(1, 31)]
    [int]$VideoQuality = 3,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [ValidateRange(32, 384)]
    [int]$AudioBitrateKbps = 64,

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
    throw "MPEG-1 YUV420 dimensions must be even: ${Width}x${Height}."
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

# Keep this curve in sync with encode_bpv.ps1 and the primary HLV preset.
# There is no EQ, loudness filter, standalone volume stage or limiter.
$audioConversion = "aformat=channel_layouts=mono,aresample=32000"
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
    -ac 1 -ar 32000 -f null NUL 2>&1
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
$videoFilter = (
    "scale=${Width}:${Height}:force_original_aspect_ratio=increase:" +
    "force_divisible_by=2:flags=lanczos," +
    "crop=${Width}:${Height}:(iw-${Width})/2:(ih-${Height})/2," +
    "setsar=1,format=yuv420p"
)

$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $InputFile,
    "-map", "0:v:0", "-map", "0:a:0",
    "-vf", $videoFilter,
    "-af", $audioFilter,
    "-fps_mode", "cfr",
    "-c:v", "mpeg1video",
    "-q:v", $VideoQuality,
    "-g", $Gop,
    "-bf", "0",
    "-pix_fmt", "yuv420p",
    "-threads", $Threads,
    "-c:a", "mp2",
    "-b:a", "${AudioBitrateKbps}k",
    "-ac", "1",
    "-ar", "32000",
    "-packetsize", "2048",
    "-max_muxing_queue_size", "1024"
)
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
$arguments += @("-f", "mpeg", $OutputFile)

Write-Host (
    "Encoding constrained MPEG-1/PS: ${Width}x${Height}, native FPS, " +
    "q=$VideoQuality, GOP $Gop, no B pictures, MP2 mono 32 kHz " +
    "${AudioBitrateKbps} kbps, $Threads FFmpeg threads..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg MPEG-1 encoding failed with exit code $LASTEXITCODE."
}

Write-Host "Checking for forbidden B pictures..."
$pictureTypes = & $ffprobe -v error -select_streams v:0 `
    -show_entries frame=pict_type -of csv=p=0 $OutputFile
if ($LASTEXITCODE -ne 0 -or
    ($pictureTypes | Where-Object { $_.Trim() -eq "B" })) {
    throw "The MPEG-1 output contains B pictures or could not be inspected."
}

Write-Host "Decoding the complete MPEG-1/MP2 file with FFmpeg..."
& $ffmpeg -v error -i $OutputFile -map 0:v:0 -map 0:a:0 `
    -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full MPEG-1/MP2 validation failed."
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
