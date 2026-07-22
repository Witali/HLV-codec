[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf"
& (Join-Path $project "flash.ps1") -Port $Port -Baud $Baud `
    -SkipBuild:$SkipBuild
