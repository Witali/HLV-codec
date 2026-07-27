#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateRange(2, 31)]
    [int]$Quality = 5,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$OutputFile,

    [string]$SelectionFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"

if (-not $OutputFile) {
    $OutputFile = Join-Path $repo `
        ("out\MJPEG\BigBuckBunny_1080p_mjpeg_q{0}_native-fps_320x180.avi" -f
            $Quality)
}
if (-not $SelectionFile) {
    $SelectionFile = Join-Path $repo "out\play.txt"
}
if (-not [IO.Path]::IsPathRooted($OutputFile)) {
    $OutputFile = [IO.Path]::GetFullPath($OutputFile)
}
if (-not [IO.Path]::IsPathRooted($SelectionFile)) {
    $SelectionFile = [IO.Path]::GetFullPath($SelectionFile)
}

if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p source is missing: $source"
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    throw "FFmpeg is unavailable: $ffmpeg"
}

$outputParent = Split-Path $OutputFile -Parent
$selectionParent = Split-Path $SelectionFile -Parent
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
New-Item -ItemType Directory -Force -Path $selectionParent | Out-Null

# Keep this curve byte-for-byte equivalent to the default HLV Big Buck Bunny
# profile. The measurement pass computes only the compressor makeup required
# to place the processed source peak at -0.1 dBFS.
$audioConversion = "aformat=channel_layouts=mono,aresample=16000"
$audioLevelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
    "attack=0.01:release=250:knee=8:" +
    "link=maximum:detection=peak"
$audioPeakTargetDb = -0.1

Write-Host (
    "Measuring the established audio level curve on the approved 1080p MOV..."
)
$analysisFilter = (
    "$audioConversion,$audioLevelCurve," +
    "astats=metadata=0:reset=0"
)
$analysisOutput = & $ffmpeg -hide_banner -nostats -i $source `
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
    throw (
        "The level curve needs attenuation, which compressor makeup " +
        "cannot provide: $curveMakeupDb dB."
    )
}
$curveMakeupText = $curveMakeupDb.ToString("0.000", $culture)
$audioFilter = (
    "$audioConversion,${audioLevelCurve}:makeup=${curveMakeupText}dB"
)

# A fixed 320-pixel width and an automatically calculated even height preserve
# the source display aspect ratio. Big Buck Bunny is 16:9, so this is 320x180.
$videoFilter = (
    "scale=320:-2:flags=lanczos," +
    "setsar=1,format=yuvj420p"
)
$arguments = @(
    "-y", "-hide_banner", "-loglevel", "error",
    "-i", $source,
    "-map", "0:v:0", "-map", "0:a:0",
    "-vf", $videoFilter,
    "-af", $audioFilter,
    "-c:v", "mjpeg",
    "-q:v", $Quality,
    "-threads:v", $Threads,
    "-pix_fmt", "yuvj420p",
    "-fps_mode", "passthrough",
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
    "Encoding MJPEG AVI: width 320, preserved aspect ratio, native FPS, " +
    "quality $Quality, $Threads threads, PCM_U8 mono 16 kHz..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "MJPEG encoding failed with exit code $LASTEXITCODE."
}

$selectedName = [IO.Path]::GetFileName($OutputFile)
Set-Content -LiteralPath $SelectionFile -Value $selectedName -Encoding ascii
$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host (
    "Selection file: $SelectionFile -> $selectedName"
)
