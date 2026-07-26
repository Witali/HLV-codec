#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 MJPEG/AVI profile.
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

    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$info = Get-TranscodeVideoInfo -InputFile $InputFile
$fpsText = Format-TranscodeNumber -Value $info.Fps
$effectiveResizeMode = if ($ResizeMode -eq "Auto") {
    if ($Height -eq 240) { "Crop" } else { "Stretch" }
}
else {
    $ResizeMode
}
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "MJPEG" `
    -FileName (
        "$($info.BaseName)_${Width}x${Height}_${fpsText}fps_MJPEG_q${Quality}.avi"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

& (Join-Path $PSScriptRoot "encode_mjpeg.ps1") `
    -InputFile $info.InputFile `
    -OutputFile $OutputFile `
    -Width $Width `
    -Height $Height `
    -ResizeMode $effectiveResizeMode `
    -Quality $Quality `
    -Threads $Threads `
    -MaxFrames $MaxFrames
if ($LASTEXITCODE -ne 0) {
    throw "MJPEG profile transcode failed."
}

