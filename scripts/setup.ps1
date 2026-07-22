[CmdletBinding()]
param(
    [switch]$ForceDownload,
    [switch]$SkipVisualStudioInstall
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent

if (-not [Environment]::Is64BitOperatingSystem) {
    throw "HLV setup requires 64-bit Windows."
}

& (Join-Path $PSScriptRoot "setup_msvc.ps1") `
    -InstallIfMissing:(-not $SkipVisualStudioInstall)
& (Join-Path $PSScriptRoot "setup_python.ps1") `
    -ForceInstall:$ForceDownload
& (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1") `
    -ForceDownload:$ForceDownload

$idfProject = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf"
& (Join-Path $idfProject "setup.ps1")

Write-Host ""
Write-Host "HLV development tools are ready."
Write-Host "Desktop build: .\scripts\build_msvc.ps1"
Write-Host "Windows player: .\scripts\build_windows_player.ps1"
Write-Host "ESP32 build:   .\scripts\build_esp32.ps1"
