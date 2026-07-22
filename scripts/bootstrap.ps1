[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
$repo = Split-Path $PSScriptRoot -Parent
$idfProject = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf"
& (Join-Path $idfProject "bootstrap.ps1")
