[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$sketch = Join-Path $repo "firmware\esp32_2432s028_hlv_player"
$buildDirectory = Join-Path $repo "build\esp32"
$littlefsImage = Join-Path $buildDirectory "littlefs.bin"
$esptoolRoot = Join-Path $repo `
    "local_tools\arduino\data\packages\esp32\tools\esptool_py"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build_esp32.ps1")
}
if (-not (Test-Path -LiteralPath $littlefsImage)) {
    throw "LittleFS image is missing. Run .\scripts\build_esp32.ps1 first."
}

try {
    & (Join-Path $PSScriptRoot "arduino.ps1") upload `
        --fqbn esp32:esp32:jczn_2432s028r `
        --board-options PartitionScheme=default `
        --input-dir $buildDirectory `
        --port $Port `
        $sketch
} catch {
    Write-Warning `
        "If ESP32 reports wrong boot mode: hold BOOT, tap RST, release BOOT, and retry."
    throw
}

$esptool = Get-ChildItem -LiteralPath $esptoolRoot -Recurse `
    -Filter esptool.exe | Select-Object -First 1 -ExpandProperty FullName
& $esptool --chip esp32 --port $Port --baud 921600 `
    write-flash 0x290000 $littlefsImage
if ($LASTEXITCODE -ne 0) {
    throw "Uploading the LittleFS video image failed."
}

Write-Host "Firmware and video.hlv were uploaded to internal flash."
