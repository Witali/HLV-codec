#requires -Version 7.4

[CmdletBinding()]
param(
    [string]$OutputFile,

    [ValidateSet("176x144", "352x288")]
    [string]$Profile = "352x288",

    [ValidateSet("Crop", "Contain")]
    [string]$FitMode = "Crop",

    [ValidateRange(0, 2048)]
    [int]$VideoBitrateKbps = 0,

    [ValidateRange(0, 4096)]
    [int]$VideoBufferKbps = 0,

    [ValidateRange(0, 31)]
    [int]$VideoQuality = 0,

    [ValidateRange(1, 300)]
    [int]$Gop = 30,

    [switch]$NoAudio,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo (
    "out\sources\big_buck_bunny_1080p_h264\" +
    "big_buck_bunny_1080p_h264.mov"
)
if (-not (Test-Path -LiteralPath $source)) {
    throw "Required project-approved 1080p source is missing: $source"
}
$profileName = if ($Profile -eq "352x288") { "CIF" } else { "QCIF" }
$effectiveBitrate = if ($VideoBitrateKbps) {
    $VideoBitrateKbps
}
elseif ($Profile -eq "352x288") {
    2048
}
else {
    384
}
if (-not $OutputFile) {
    $qualityLabel = if ($VideoQuality) {
        "q${VideoQuality}"
    }
    else {
        "${effectiveBitrate}k"
    }
    $OutputFile = Join-Path $repo (
        "out\H263\BigBuckBunny_${Profile}_nativefps_" +
        "H263_${profileName}_${qualityLabel}.avi"
    )
}

$arguments = @{
    InputFile = $source
    OutputFile = $OutputFile
    Profile = $Profile
    FitMode = $FitMode
    VideoBitrateKbps = $VideoBitrateKbps
    VideoBufferKbps = $VideoBufferKbps
    VideoQuality = $VideoQuality
    Gop = $Gop
    MaxFrames = $MaxFrames
}
if ($NoAudio) {
    $arguments.NoAudio = $true
}
if ($ReportFile) {
    $arguments.ReportFile = $ReportFile
}

& (Join-Path $PSScriptRoot "encode_h263_avi.ps1") @arguments
