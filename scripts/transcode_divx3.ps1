#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 DivX 3/AVI profile.

.DESCRIPTION
The output always uses exactly half of the nominal source frame rate.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$InputFile,

    [string]$OutputFile,

    [ValidateRange(16, 320)]
    [int]$Width = 320,

    [ValidateRange(16, 240)]
    [int]$Height = 240,

    [ValidateSet("Auto", "Stretch", "Crop")]
    [string]$ResizeMode = "Auto",

    [ValidateRange(2, 31)]
    [int]$Quality = 3,

    [ValidateRange(1, 16)]
    [int]$Threads = 6,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$NoAudio,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$info = Get-TranscodeVideoInfo -InputFile $InputFile
$outputFps = $info.Fps / 2.0
if ($outputFps -lt 1.0 -or $outputFps -gt 30.0) {
    throw "Half source rate is outside the supported 1..30 fps range."
}
$fpsText = Format-TranscodeNumber -Value $outputFps
$effectiveResizeMode = if ($ResizeMode -eq "Auto") {
    if ($Height -eq 240) { "Crop" } else { "Stretch" }
}
else {
    $ResizeMode
}
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "DivX3" `
    -FileName (
        "$($info.BaseName)_${Width}x${Height}_${fpsText}fps_DivX3_q${Quality}.avi"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

$videoFilter = Get-Esp32PreparationFilter `
    -Width $Width `
    -Height $Height `
    -ResizeMode $effectiveResizeMode `
    -Fps $outputFps
$gop = [Math]::Max(1, [Math]::Round($outputFps))
$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $info.InputFile,
    "-map", "0:v:0",
    "-vf", $videoFilter,
    "-fps_mode", "cfr",
    "-c:v", "msmpeg4",
    "-q:v", $Quality,
    "-g", $gop,
    "-bf", "0",
    "-pix_fmt", "yuv420p",
    "-threads:v", $Threads,
    "-vtag", "DIV3"
)
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames)
}
if ($NoAudio) {
    $arguments += "-an"
}
else {
    $arguments += @(
        "-map", "0:a:0?",
        "-c:a", "pcm_u8",
        "-ar", "16000",
        "-ac", "1"
    )
}
$arguments += @("-f", "avi", $OutputFile)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg DivX 3 encoding failed."
}

& $ffmpeg -v error -xerror -i $OutputFile -map 0:v:0 -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full DivX 3 validation failed."
}
$packetSizes = & $ffprobe -v error -select_streams v:0 `
    -show_entries packet=size -of csv=p=0 $OutputFile
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect DivX 3 packet sizes."
}
$maximumPacket = (
    $packetSizes |
        Where-Object { $_.Trim() } |
        ForEach-Object { [uint64]$_.Trim() } |
        Measure-Object -Maximum
).Maximum
if ($maximumPacket -gt 98304) {
    throw (
        "DivX 3 packet $maximumPacket exceeds the ESP32 profile limit " +
        "of 98304 bytes."
    )
}
$streamText = & $ffprobe -v error -count_frames -select_streams v:0 `
    -show_entries (
        "stream=codec_name,codec_tag_string,width,height," +
        "r_frame_rate,nb_read_frames"
    ) -of json $OutputFile
if ($LASTEXITCODE -ne 0) {
    throw "Could not validate the DivX 3 output metadata."
}
$stream = @(($streamText | ConvertFrom-Json).streams)[0]
if ($stream.codec_name -ne "msmpeg4v3" -or
    $stream.codec_tag_string -ne "DIV3" -or
    $stream.width -ne $Width -or
    $stream.height -ne $Height -or
    $stream.r_frame_rate -notmatch "^(\d+)/(\d+)$") {
    throw "Output does not match the requested DivX 3 profile."
}
$actualFps = [double]$Matches[1] / [double]$Matches[2]
if ([Math]::Abs($actualFps - $outputFps) -gt 0.001) {
    throw "DivX 3 output is not exactly half of the source frame rate."
}
$reportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
[ordered]@{
    input = $info.InputFile
    output = $OutputFile
    codec = "DivX3"
    codecTag = "DIV3"
    width = $Width
    height = $Height
    sourceFps = $info.Fps
    fps = $actualFps
    frameRateRule = "source/2"
    frames = [int]$stream.nb_read_frames
    quality = $Quality
    gop = $gop
    bFrames = 0
    maximumVideoPacketBytes = [uint64]$maximumPacket
    maximumAllowedPacketBytes = 98304
    audio = if ($NoAudio) { $null } else { "PCM_U8 mono 16000 Hz" }
} | ConvertTo-Json | Set-Content -LiteralPath $reportFile -Encoding utf8
Write-Host (
    "Ready: {0}; {1} fps (source/2), maximum packet {2:N0} bytes" -f
    $OutputFile, $outputFps, $maximumPacket
)
Write-Host "Report: $reportFile"
