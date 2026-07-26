#requires -Version 7.4

# Reproducible MPEG-1/MP2 preset for out/sources/VID_20260522_181611.mp4.
[CmdletBinding()]
param(
    [ValidateRange(1, 31)]
    [int]$VideoQuality = 3,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [ValidateRange(32, 384)]
    [int]$AudioBitrateKbps = 64,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$OutputFile,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "out\sources\VID_20260522_181611.mp4"

if (-not $OutputFile) {
    $OutputFile = Join-Path $repo (
        "out\MPEG1\VID_20260522_181611_center-crop_240x180_" +
        "mpeg1_q${VideoQuality}_native-fps.mpg"
    )
}
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
if (-not (Test-Path -LiteralPath $source)) {
    throw "Required source is missing: $source"
}

& (Join-Path $PSScriptRoot "encode_mpeg1.ps1") `
    -InputFile $source `
    -OutputFile $OutputFile `
    -ReportFile $ReportFile `
    -Width 240 `
    -Height 180 `
    -VideoQuality $VideoQuality `
    -Threads $Threads `
    -Gop $Gop `
    -AudioBitrateKbps $AudioBitrateKbps `
    -MaxFrames $MaxFrames
