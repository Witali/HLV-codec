#requires -Version 7.4

# Encode the bounded ESP32 MPEG-4 Part 2 Simple Profile in an M4S2 AVI.
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [string]$OutputFile,

    [ValidateSet("Crop", "Contain")]
    [string]$FitMode = "Crop",

    # Q5 is the calibrated 35 dB quality default accepted for production.
    [ValidateRange(1, 31)]
    [int]$VideoQuality = 5,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [ValidateSet("Standard", "Esp32Speed")]
    [string]$Preset = "Standard",

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [switch]$NoAudio,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_audio_normalization.ps1")
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
        "ESP32 MPEG-4 Simple Profile requires a full source rate from " +
        "1 to 30 fps; refusing to reduce ${sourceFps} fps."
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
    $profileLabel = if ($Preset -eq "Esp32Speed") {
        "MPEG4SP_SPEED"
    }
    elseif ($VideoQuality -eq 5) {
        "MPEG4SP_35dB"
    }
    else {
        "MPEG4SP_M4S2"
    }
    $qualitySuffix = if ($Preset -eq "Standard" -and
        $VideoQuality -eq 5) { "" } else { "_q${VideoQuality}" }
    $OutputFile = Join-Path $repo (
        "out\MPEG4SP\${baseName}_320x240_${fpsLabel}fps_" +
        "${profileLabel}${qualitySuffix}.avi"
    )
}
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
if ([IO.Path]::GetExtension($OutputFile) -ne ".avi") {
    throw "MPEG-4 Simple Profile encoding is restricted to AVI."
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

$videoFilter = if ($FitMode -eq "Crop") {
    (
        "setpts=N/(${sourceRateExpression}*TB)," +
        "crop=" +
        "'trunc(min(iw\,ih*4/3)/2)*2':" +
        "'trunc(min(ih\,iw*3/4)/2)*2':" +
        "(iw-ow)/2:(ih-oh)/2," +
        "scale=320:240:flags=lanczos+accurate_rnd+full_chroma_int," +
        "setsar=1,format=yuv420p"
    )
}
else {
    (
        "setpts=N/(${sourceRateExpression}*TB)," +
        "pad=" +
        "'ceil(max(iw\,ih*4/3)/2)*2':" +
        "'ceil(max(ih\,iw*3/4)/2)*2':" +
        "(ow-iw)/2:(oh-ih)/2:black," +
        "scale=320:240:flags=lanczos+accurate_rnd+full_chroma_int," +
        "setsar=1,format=yuv420p"
    )
}

$audioNormalization = $null
$audioRate = 0
if (-not $NoAudio) {
    $audioIndex = & $ffprobe -v error -select_streams a:0 `
        -show_entries stream=index -of csv=p=0 $InputFile |
        Select-Object -First 1
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe audio inspection failed."
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$audioIndex)) {
        $audioRate = Get-PrimaryAudioSampleRate `
            -Ffprobe $ffprobe -InputFile $InputFile
        $audioNormalization = Get-PeakSafeAudioFilter `
            -Ffmpeg $ffmpeg `
            -InputFile $InputFile `
            -Rate $audioRate
    }
}

$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $InputFile,
    "-map", "0:v:0",
    "-vf", $videoFilter,
    "-fps_mode", "cfr",
    "-c:v", "mpeg4",
    "-tag:v", "M4S2",
    "-q:v", $VideoQuality,
    "-g", $Gop,
    "-bf", "0",
    "-flags", "+global_header",
    "-mpeg_quant", "0",
    "-data_partitioning", "0",
    "-alternate_scan", "0",
    "-pix_fmt", "yuv420p",
    "-threads", $Threads
)
if ($Preset -eq "Esp32Speed") {
    $arguments += @(
        "-motion_est", "zero",
        "-mpv_flags", "+skip_rd",
        "-luma_elim_threshold", "4",
        "-chroma_elim_threshold", "4"
    )
}
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
if ($NoAudio -or -not $audioNormalization) {
    $arguments += "-an"
}
else {
    $arguments += @(
        "-map", "0:a:0?",
        "-c:a", "adpcm_ima_wav",
        "-ar", "$audioRate",
        "-ac", "1"
    )
    if ($audioNormalization) {
        $arguments += @("-af", $audioNormalization.Filter)
    }
    if ($MaxFrames) {
        $arguments += "-shortest"
    }
}
$arguments += @("-f", "avi", $OutputFile)

Write-Host (
    "Encoding MPEG-4 Simple Profile/M4S2 AVI: 320x240, " +
    "full source rate $($sourceFps.ToString("0.###", $culture)) fps, " +
    "q=$VideoQuality, GOP $Gop, $FitMode fit, $Preset preset..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg MPEG-4 Simple Profile encoding failed."
}

$report = & $ffprobe -v error -count_frames -show_format `
    -show_streams -of json $OutputFile
if ($LASTEXITCODE -ne 0) {
    throw "FFprobe report generation failed."
}
$metadata = $report | ConvertFrom-Json
$video = @($metadata.streams |
    Where-Object { $_.codec_type -eq "video" })
if ($video.Count -ne 1 -or
    $video[0].codec_name -ne "mpeg4" -or
    $video[0].profile -ne "Simple Profile" -or
    $video[0].codec_tag_string -ne "M4S2" -or
    $video[0].width -ne 320 -or $video[0].height -ne 240 -or
    $video[0].pix_fmt -ne "yuv420p" -or
    $video[0].has_b_frames -ne 0) {
    throw "Output is not the bounded 320x240 MPEG-4 SP/M4S2 profile."
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
    throw "The -NoAudio output unexpectedly contains audio."
}
if ($audio.Count -gt 1) {
    throw "Output unexpectedly contains more than one audio track."
}
if ($audio.Count -eq 1 -and
    ($audio[0].codec_name -ne "adpcm_ima_wav" -or
     $audio[0].sample_rate -ne "$audioRate" -or
     $audio[0].channels -ne 1)) {
    throw "Output audio is not IMA ADPCM mono $audioRate Hz."
}

Write-Host "Decoding the complete MPEG-4 SP/AVI file with FFmpeg..."
& $ffmpeg -v error -xerror -i $OutputFile `
    -map 0:v:0 -map 0:a:0? -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full MPEG-4 SP/AVI validation failed."
}
if ($audioNormalization) {
    Write-AudioNormalizationStatus $audioNormalization
}

$report | Set-Content -LiteralPath $ReportFile -Encoding utf8
$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host "Report: $ReportFile"
