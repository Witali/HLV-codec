#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 H.263 CIF/AVI profile.
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
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "H263" `
    -FileName (
        "$($info.BaseName)_352x288_${fpsText}fps_H263_CIF_2048kbps.avi"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

$arguments = @{
    InputFile = $info.InputFile
    OutputFile = $OutputFile
    Profile = "352x288"
    FitMode = "Crop"
    VideoBitrateKbps = 2048
    VideoBufferKbps = 2048
    Gop = 1
    Threads = $Threads
    MaxFrames = $MaxFrames
}
if ($NoAudio) {
    $arguments.NoAudio = $true
}
& (Join-Path $PSScriptRoot "encode_h263_avi.ps1") @arguments
if ($LASTEXITCODE -ne 0) {
    throw "H.263 profile transcode failed."
}
