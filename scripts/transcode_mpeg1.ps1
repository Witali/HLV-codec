#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 MPEG-1 Program Stream profile.
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

    [ValidateRange(1, 31)]
    [int]$Quality = 3,

    [ValidateRange(1, 16)]
    [int]$Threads = 6,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$info = Get-TranscodeVideoInfo -InputFile $InputFile
$fpsText = Format-TranscodeNumber -Value $info.Fps
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "MPEG1" `
    -FileName (
        "$($info.BaseName)_${Width}x${Height}_${fpsText}fps_MPEG1_q${Quality}.mpg"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

& (Join-Path $PSScriptRoot "encode_mpeg1.ps1") `
    -InputFile $info.InputFile `
    -OutputFile $OutputFile `
    -Width $Width `
    -Height $Height `
    -VideoQuality $Quality `
    -Threads $Threads `
    -Gop 30 `
    -AudioBitrateKbps 64 `
    -MaxFrames $MaxFrames
if ($LASTEXITCODE -ne 0) {
    throw "MPEG-1 profile transcode failed."
}
