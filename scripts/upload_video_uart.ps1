[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [string]$Name = "video.hlv",
    [ValidateSet(460800, 921600, 1500000, 2000000)]
    [int]$DataBaud = 2000000
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf"
& (Join-Path $project "upload-video.ps1") -Port $Port -File $File `
    -Name $Name -DataBaud $DataBaud
