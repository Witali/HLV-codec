[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

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

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [switch]$NoAudio,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$InputFile = [IO.Path]::GetFullPath($InputFile)
if (-not $OutputFile) {
    $baseName = [IO.Path]::GetFileNameWithoutExtension($InputFile)
    $OutputFile = Join-Path $repo (
        "out\${baseName}_h263_${Profile}_${VideoBitrateKbps}k_pcm.avi"
    )
}

$arguments = @{
    InputFile = $InputFile
    OutputFile = $OutputFile
    Container = "avi"
    Profile = $Profile
    FitMode = $FitMode
    Fps = $Fps
    VideoBitrateKbps = $VideoBitrateKbps
    VideoBufferKbps = $VideoBufferKbps
    Gop = $Gop
    Threads = $Threads
    MaxFrames = $MaxFrames
}
if ($NoAudio) {
    $arguments.NoAudio = $true
}
if ($ReportFile) {
    $arguments.ReportFile = $ReportFile
}

& (Join-Path $PSScriptRoot "encode_h263_3gp.ps1") @arguments
