#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(1, 65535)]
    [int]$Gop = 48,

    [ValidateRange(0, 1000000000)]
    [double]$Lambda = 64,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$OutputFile,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
if (-not $OutputFile) {
    $OutputFile = Join-Path $repo `
        "out\BigBuckBunny_1080p_bpv1_v3_lambda64_normalized_native-fps_320x180.bpv1"
}
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
if (-not (Test-Path -LiteralPath $source)) {
    throw "Required project-approved 1080p source is missing: $source"
}

& (Join-Path $PSScriptRoot "encode_bpv.ps1") `
    -InputFile $source `
    -OutputFile $OutputFile `
    -ReportFile $ReportFile `
    -Width 320 `
    -Height 180 `
    -Threads $Threads `
    -Gop $Gop `
    -Lambda $Lambda `
    -MaxFrames $MaxFrames
