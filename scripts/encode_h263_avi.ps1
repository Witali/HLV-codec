#requires -Version 7.4

# Encode only standard baseline H.263 QCIF/CIF in AVI at the source frame rate.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [string]$OutputFile,

    [ValidateSet("176x144", "352x288")]
    [string]$Profile = "352x288",

    [ValidateSet("Crop", "Contain")]
    [string]$FitMode = "Crop",

    [ValidateRange(0, 2048)]
    [int]$VideoBitrateKbps = 0,

    [ValidateRange(0, 4096)]
    [int]$VideoBufferKbps = 0,

    [ValidateRange(0, 31)]
    [int]$VideoQuality = 0,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [switch]$NoAudio,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$culture = [Globalization.CultureInfo]::InvariantCulture

$InputFile = [IO.Path]::GetFullPath($InputFile)
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

$profileSize = $Profile.Split("x")
$width = [int]$profileSize[0]
$height = [int]$profileSize[1]
$isCif = $Profile -eq "352x288"
$profileName = if ($isCif) { "CIF" } else { "QCIF" }
$effectiveGop = if ($isCif) { 1 } else { $Gop }
$constantQuality = $VideoQuality -gt 0
if (-not $constantQuality -and -not $VideoBitrateKbps) {
    $VideoBitrateKbps = if ($isCif) { 2048 } else { 384 }
}
if (-not $constantQuality -and -not $VideoBufferKbps) {
    $VideoBufferKbps = if ($isCif) { 1024 } else { 512 }
}

$inputReportText = & $ffprobe -v error `
    -select_streams v:0 `
    -show_entries stream=avg_frame_rate,r_frame_rate `
    -of json $InputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe could not inspect the input frame rate."
}
$inputMetadata = $inputReportText | ConvertFrom-Json
$inputVideo = @($inputMetadata.streams)
if ($inputVideo.Count -ne 1) {
    throw "Input must contain one primary video stream."
}
# MP4 average rates may differ slightly from the nominal cadence because the
# final frame has a shorter duration. Use the declared cadence first so a
# nominal 30 fps stream is not misclassified as being above the H.263 limit.
$sourceRate = [string]$inputVideo[0].r_frame_rate
if (-not $sourceRate -or $sourceRate -eq "0/0") {
    $sourceRate = [string]$inputVideo[0].avg_frame_rate
}
if ($sourceRate -notmatch "^(\d+)/(\d+)$" -or
    [double]$Matches[2] -eq 0.0) {
    throw "Input has an invalid frame rate: $sourceRate"
}
$sourceFps = [double]$Matches[1] / [double]$Matches[2]
if ($sourceFps -lt 1.0 -or $sourceFps -gt 30.001) {
    throw (
        "Baseline H.263 requires a full source rate from 1 to 30 fps; " +
        "refusing to reduce ${sourceFps} fps."
    )
}
$fpsLabel = if ([Math]::Abs(
    $sourceFps - [Math]::Round($sourceFps)
) -lt 0.0005) {
    [Math]::Round($sourceFps).ToString("0", $culture)
}
else {
    $sourceFps.ToString("0.###", $culture).Replace(".", "p")
}
$sourceRateExpression = $sourceFps.ToString("0.########", $culture)

if (-not $OutputFile) {
    $baseName = [IO.Path]::GetFileNameWithoutExtension($InputFile)
    $qualityLabel = if ($constantQuality) {
        "q${VideoQuality}"
    }
    else {
        "${VideoBitrateKbps}k"
    }
    $OutputFile = Join-Path $repo (
        "out\H263\${baseName}_${Profile}_${fpsLabel}fps_" +
        "H263_${profileName}_${qualityLabel}.avi"
    )
}
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
if ([IO.Path]::GetExtension($OutputFile) -ne ".avi") {
    throw "H.263 encoding is restricted to the AVI container."
}
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
$ReportFile = [IO.Path]::GetFullPath($ReportFile)
New-Item -ItemType Directory -Force -Path (
    Split-Path $OutputFile -Parent
) | Out-Null
New-Item -ItemType Directory -Force -Path (
    Split-Path $ReportFile -Parent
) | Out-Null

# QCIF is shown as a centered 176x144 picture on the 320x240 display. CIF is
# decoded at its mandatory 352x288 coded size, but the player displays its
# centered 320x240 window. Prepare that exact window here and surround it with
# the 16/24-pixel CIF guard area so no useful picture is cropped on playback.
$contentWidth = if ($isCif) { 320 } else { $width }
$contentHeight = if ($isCif) { 240 } else { $height }
$cifPadding = if ($isCif) { ",pad=${width}:${height}:16:24:black" } else { "" }
$videoFilter = if ($FitMode -eq "Crop") {
    (
        "setpts=N/(${sourceRateExpression}*TB)," +
        "crop=" +
        "'trunc(min(iw\,ih*4/3)/2)*2':" +
        "'trunc(min(ih\,iw*3/4)/2)*2':" +
        "(iw-ow)/2:(ih-oh)/2," +
        "scale=${contentWidth}:${contentHeight}:" +
        "flags=lanczos+accurate_rnd+full_chroma_int," +
        "setsar=1${cifPadding},format=yuv420p"
    )
}
else {
    (
        "setpts=N/(${sourceRateExpression}*TB)," +
        "pad=" +
        "'ceil(max(iw\,ih*4/3)/2)*2':" +
        "'ceil(max(ih\,iw*3/4)/2)*2':" +
        "(ow-iw)/2:(oh-ih)/2:black," +
        "scale=${contentWidth}:${contentHeight}:" +
        "flags=lanczos+accurate_rnd+full_chroma_int," +
        "setsar=1${cifPadding},format=yuv420p"
    )
}

$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $InputFile,
    "-map", "0:v:0",
    "-vf", $videoFilter,
    "-fps_mode", "cfr",
    "-c:v", "h263",
    "-tag:v", "H263",
    "-g", $effectiveGop,
    "-bf", "0",
    "-pix_fmt", "yuv420p",
    "-threads", $Threads
)
if ($constantQuality) {
    $arguments += @("-q:v", $VideoQuality)
}
else {
    $arguments += @(
        "-b:v", "${VideoBitrateKbps}k",
        "-maxrate", "${VideoBitrateKbps}k",
        "-bufsize", "${VideoBufferKbps}k"
    )
}
if ($isCif) {
    # CIF remains intra-only for the bounded ESP32 decoder memory profile.
    $arguments += @(
        "-mbd", "rd",
        "-trellis", "2",
        "-mpv_flags", "+qp_rd+cbp_rd"
    )
}
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
if ($NoAudio) {
    $arguments += "-an"
}
else {
    $arguments += @(
        "-map", "0:a:0?",
        "-c:a", "pcm_s16le",
        "-ar", "8000",
        "-ac", "1"
    )
}
$arguments += @("-f", "avi", $OutputFile)

Write-Host (
    (
        "Encoding baseline H.263/AVI: {0} {1}, full source rate " +
        "{2} fps, {3}, {4} fit, GOP {5}, {6}..."
    ) -f
    $profileName,
    $Profile,
    $sourceFps.ToString("0.###", $culture),
    $(if ($constantQuality) {
        "constant quality q=$VideoQuality"
    }
    else {
        "${VideoBitrateKbps} kbps, VBV ${VideoBufferKbps}k"
    }),
    $FitMode,
    $effectiveGop,
    $(if ($NoAudio) {
        "video only"
    }
    else {
        "PCM S16LE mono 8 kHz"
    })
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg H.263/AVI encoding failed with exit code $LASTEXITCODE."
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
    $video[0].codec_tag_string -ne "H263" -or
    $video[0].width -ne $width -or $video[0].height -ne $height) {
    throw "Output is not standard $profileName H.263 in AVI."
}
$outputRate = [string]$video[0].avg_frame_rate
if ($outputRate -notmatch "^(\d+)/(\d+)$" -or
    [double]$Matches[2] -eq 0.0) {
    throw "Output has an invalid frame rate: $outputRate"
}
$outputFps = [double]$Matches[1] / [double]$Matches[2]
if ([Math]::Abs($outputFps - $sourceFps) -gt 0.001) {
    throw (
        "Output frame rate ${outputFps} fps does not preserve the " +
        "full source rate ${sourceFps} fps."
    )
}

$audio = @($metadata.streams |
    Where-Object { $_.codec_type -eq "audio" })
if ($NoAudio -and $audio.Count) {
    throw "The -NoAudio output unexpectedly contains an audio track."
}
if ($audio.Count -gt 1) {
    throw "Output unexpectedly contains more than one audio track."
}
if ($audio.Count -eq 1 -and
    ($audio[0].codec_name -ne "pcm_s16le" -or
     $audio[0].sample_rate -ne "8000" -or
     $audio[0].channels -ne 1)) {
    throw "Output audio is not PCM S16LE mono 8 kHz."
}

Write-Host "Decoding the complete H.263/AVI file with FFmpeg..."
& $ffmpeg -v error -xerror -i $OutputFile `
    -map 0:v:0 -map 0:a:0? -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full H.263/AVI validation failed."
}

$report | Set-Content -LiteralPath $ReportFile -Encoding utf8
$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host "Report: $ReportFile"
