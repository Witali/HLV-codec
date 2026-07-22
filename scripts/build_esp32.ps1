[CmdletBinding()]
param(
    [string]$OutputDirectory = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) "build\esp32"),
    [string]$VideoFile = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) "out\video.hlv")
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$cli = Join-Path $repo "tools\arduino-cli\arduino-cli.exe"
$platform = Join-Path $repo `
    "local_tools\arduino\data\packages\esp32\hardware\esp32\3.3.8\platform.txt"
$lovyanGfx = Join-Path $repo `
    "local_tools\arduino\user\libraries\LovyanGFX\library.properties"
$sketch = Join-Path $repo "firmware\esp32_2432s028_hlv_player"

if (-not (Test-Path -LiteralPath $cli) -or
    -not (Test-Path -LiteralPath $platform) -or
    -not (Test-Path -LiteralPath $lovyanGfx)) {
    & (Join-Path $PSScriptRoot "bootstrap_arduino.ps1")
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

if (-not (Test-Path -LiteralPath $VideoFile)) {
    & (Join-Path $PSScriptRoot "prepare_esp32_video.ps1") `
        -OutputFile $VideoFile
}

& (Join-Path $PSScriptRoot "arduino.ps1") compile `
    --fqbn esp32:esp32:jczn_2432s028r `
    --board-options PartitionScheme=default `
    --library $repo `
    --output-dir $OutputDirectory `
    $sketch

& (Join-Path $PSScriptRoot "build_littlefs.ps1") `
    -InputFile $VideoFile `
    -OutputFile (Join-Path $OutputDirectory "littlefs.bin")

Write-Host "ESP32 firmware and internal video image are ready in $OutputDirectory"
