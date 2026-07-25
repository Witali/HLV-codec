# Encode the first 3GP/H.263 profile supported by both project players.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [Parameter(Mandatory)]
    [string]$OutputFile,

    [ValidateRange(1, 30)]
    [int]$Fps = 15,

    [ValidateRange(16, 512)]
    [int]$VideoBitrateKbps = 128,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateSet("4.75k", "5.15k", "5.9k", "6.7k", "7.4k",
        "7.95k", "10.2k", "12.2k")]
    [string]$AudioBitrate = "12.2k",

    [switch]$NoAudio,

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

# Baseline H.263 uses the standardized QCIF canvas. Preserve the source aspect
# ratio inside 176x144 and letterbox/pillarbox instead of stretching it.
$videoFilter = (
    "fps=${Fps}," +
    "scale=176:144:force_original_aspect_ratio=decrease:" +
    "force_divisible_by=2:flags=lanczos," +
    "pad=176:144:(ow-iw)/2:(oh-ih)/2:black," +
    "setsar=1,format=yuv420p"
)
$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $InputFile,
    "-map", "0:v:0",
    "-vf", $videoFilter,
    "-fps_mode", "cfr",
    "-c:v", "h263",
    "-b:v", "${VideoBitrateKbps}k",
    "-maxrate", "${VideoBitrateKbps}k",
    "-bufsize", "$($VideoBitrateKbps * 2)k",
    "-g", $Gop,
    "-bf", "0",
    "-pix_fmt", "yuv420p",
    "-threads", $Threads,
    "-movflags", "+faststart"
)
if ($NoAudio) {
    $arguments += @("-an")
} else {
    $arguments += @(
        "-map", "0:a:0?",
        "-c:a", "libopencore_amrnb",
        "-ar", "8000",
        "-ac", "1",
        "-b:a", $AudioBitrate,
        "-shortest"
    )
}
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
$arguments += @("-f", "3gp", $OutputFile)

Write-Host (
    "Encoding baseline H.263/3GP: 176x144 QCIF, ${Fps} fps, " +
    "${VideoBitrateKbps} kbps, GOP $Gop, " +
    $(if ($NoAudio) { "video only" } else {
        "AMR-NB mono 8 kHz at $AudioBitrate"
    }) + "..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg H.263/3GP encoding failed with exit code $LASTEXITCODE."
}

$report = & $ffprobe -v error -count_frames -show_format `
    -show_streams -of json $OutputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe report generation failed."
}
$metadata = $report | ConvertFrom-Json
$video = @($metadata.streams |
    Where-Object { $_.codec_type -eq "video" })
if ($video.Count -ne 1 -or $video[0].codec_name -ne "h263" -or
    $video[0].width -ne 176 -or $video[0].height -ne 144) {
    throw "Output is not the supported 176x144 H.263 profile."
}
$audio = @($metadata.streams |
    Where-Object { $_.codec_type -eq "audio" })
if ($NoAudio -and $audio.Count) {
    throw "The -NoAudio output unexpectedly contains an audio track."
}
if ($audio.Count -gt 1 -or
    ($audio.Count -eq 1 -and
        ($audio[0].codec_name -ne "amr_nb" -or
         $audio[0].sample_rate -ne "8000" -or
         $audio[0].channels -ne 1))) {
    throw "Output audio is not the supported mono AMR-NB 8 kHz profile."
}

Write-Host "Decoding the complete H.263/3GP file with FFmpeg..."
& $ffmpeg -v error -i $OutputFile -map 0:v:0 -map 0:a:0? -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full H.263/3GP validation failed."
}

$report | Set-Content -LiteralPath $ReportFile -Encoding utf8
$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host "Report: $ReportFile"
