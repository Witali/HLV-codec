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
. (Join-Path $PSScriptRoot "_audio_normalization.ps1")
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"

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
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    throw "Repository-local FFmpeg tools are unavailable."
}

$outputParent = Split-Path $OutputFile -Parent
$selectionParent = Split-Path $SelectionFile -Parent
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
New-Item -ItemType Directory -Force -Path $selectionParent | Out-Null

$audioRate = Get-PrimaryAudioSampleRate -Ffprobe $ffprobe -InputFile $source
$audioNormalization = Get-PeakSafeAudioFilter -Ffmpeg $ffmpeg `
    -InputFile $source -Rate $audioRate

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
    "-af", $audioNormalization.Filter,
    "-c:v", "mjpeg",
    "-q:v", $Quality,
    "-threads:v", $Threads,
    "-pix_fmt", "yuvj420p",
    "-fps_mode", "passthrough",
    "-c:a", "adpcm_ima_wav",
    "-ar", "$audioRate",
    "-ac", "1",
    "-shortest"
)
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
$arguments += @("-f", "avi", $OutputFile)

Write-Host (
    "Encoding MJPEG AVI: width 320, preserved aspect ratio, native FPS, " +
    "quality $Quality, $Threads threads, IMA ADPCM mono $audioRate Hz..."
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
