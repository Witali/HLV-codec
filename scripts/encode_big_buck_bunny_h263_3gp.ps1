[CmdletBinding()]
param(
    [string]$OutputFile,
    [ValidateSet("176x144", "256x144", "256x192",
        "320x180", "320x240")]
    [string]$Profile = "320x240",
    [ValidateSet("Crop", "Contain")]
    [string]$FitMode = "Crop",
    [ValidateRange(1, 30)]
    [int]$Fps = 15,
    [ValidateRange(16, 2048)]
    [int]$VideoBitrateKbps = 1536,
    [ValidateRange(32, 4096)]
    [int]$VideoBufferKbps = 1024,
    [ValidateRange(1, 300)]
    [int]$Gop = 30,
    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo (
    "out\sources\big_buck_bunny_1080p_h264\" +
    "big_buck_bunny_1080p_h264.mov"
)
if (-not $OutputFile) {
    $OutputFile = Join-Path $repo (
        "out\BigBuckBunny_1080p_h263_${VideoBitrateKbps}k_" +
        "${Fps}fps_${Profile}.3gp"
    )
}

& (Join-Path $PSScriptRoot "encode_h263_3gp.ps1") `
    -InputFile $source `
    -OutputFile $OutputFile `
    -Profile $Profile `
    -FitMode $FitMode `
    -Fps $Fps `
    -VideoBitrateKbps $VideoBitrateKbps `
    -VideoBufferKbps $VideoBufferKbps `
    -Gop $Gop `
    -MaxFrames $MaxFrames
