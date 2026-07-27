[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo "firmware\esp32_2432s028_hlv_player_id_c"
& (Join-Path $project "build.ps1") -Clean:$Clean
