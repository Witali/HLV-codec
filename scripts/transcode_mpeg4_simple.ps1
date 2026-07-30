#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 MPEG-4 Simple Profile AVI.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$InputFile,

    [string]$OutputFile,

    [ValidateRange(1, 16)]
    [int]$Threads = 6,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$NoAudio,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$info = Get-TranscodeVideoInfo -InputFile $InputFile
$fpsText = Format-TranscodeNumber -Value $info.Fps
$defaultQuality = 5
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "MPEG4SP" `
    -FileName (
        "$($info.BaseName)_320x240_${fpsText}fps_" +
        "MPEG4SP_M4S2_q${defaultQuality}.avi"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

$arguments = @{
    InputFile = $info.InputFile
    OutputFile = $OutputFile
    FitMode = "Crop"
    VideoQuality = $defaultQuality
    Gop = 30
    Threads = $Threads
    MaxFrames = $MaxFrames
}
if ($NoAudio) {
    $arguments.NoAudio = $true
}
& (Join-Path $PSScriptRoot "encode_mpeg4_simple_avi.ps1") @arguments
if ($LASTEXITCODE -ne 0) {
    throw "MPEG-4 Simple Profile transcode failed."
}
