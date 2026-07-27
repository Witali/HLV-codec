#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to the production ESP32 BPV v6 profile.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$InputFile,

    [string]$OutputDirectory,

    [ValidateRange(2, 320)]
    [int]$Width = 320,

    [ValidateRange(2, 240)]
    [int]$Height = 240,

    [ValidateSet("Auto", "Stretch", "Crop")]
    [string]$ResizeMode = "Auto",

    [ValidateRange(10.0, 99.0)]
    [double]$TargetPsnrDb = 40.0,

    [ValidateRange(1, 16)]
    [int]$Threads = 6,

    [ValidateSet("Cpu", "Auto", "Cuda")]
    [string]$Device = "Cuda",

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$NoAudio,
    [switch]$PixelMotion,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$info = Get-TranscodeVideoInfo -InputFile $InputFile
$repo = Split-Path $PSScriptRoot -Parent
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repo "out\BPV"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$effectiveResizeMode = if ($ResizeMode -eq "Auto") {
    if ($Height -eq 240) { "Crop" } else { "Stretch" }
}
else {
    $ResizeMode
}

$arguments = @{
    InputFile = @($info.InputFile)
    OutputName = @($info.BaseName)
    OutputDirectory = $OutputDirectory
    TargetPsnrDb = $TargetPsnrDb
    Width = $Width
    Height = $Height
    ResizeMode = $effectiveResizeMode
    Fps = 0
    Threads = $Threads
    Device = $Device
    Gop = 48
    CandidatePalettes = 8
    ActivePalettes = $true
    ToleranceDb = 0.10
    InitialLambda = 64
    MaximumLambda = 4096
    MaxFrames = $MaxFrames
    NoSummary = $true
}
if ($NoAudio) {
    $arguments.NoAudio = $true
}
if ($PixelMotion) {
    $arguments.PixelMotion = $true
}
if ($Force) {
    $arguments.Force = $true
}
& (Join-Path $PSScriptRoot "encode_bpv_target_quality.ps1") @arguments
if ($LASTEXITCODE -ne 0) {
    throw "BPV v6 profile transcode failed."
}
