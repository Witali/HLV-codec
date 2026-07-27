[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$tools = Join-Path $project ".tools"
$idf = Join-Path $tools "esp-idf-v5.5.5"
$idfTools = Join-Path $tools "espressif"
$python = Join-Path $tools "python"
$baseMarker = Join-Path $tools "ready-v5.5.5"
$qemuMarker = Join-Path $tools "qemu-ready-v5.5.5"

if (-not (Test-Path -LiteralPath $baseMarker)) {
    & (Join-Path $project "setup.ps1")
}
if (Test-Path -LiteralPath $qemuMarker) {
    Write-Host "Project-local ESP32 QEMU is ready."
    return
}

$savedPath = $env:Path
$savedIdfPath = $env:IDF_PATH
$savedIdfToolsPath = $env:IDF_TOOLS_PATH
$savedPythonNoUserSite = $env:PYTHONNOUSERSITE
try {
    $env:IDF_PATH = $idf
    $env:IDF_TOOLS_PATH = $idfTools
    $env:PYTHONNOUSERSITE = "1"
    $env:Path = "$python;$python\Scripts;$env:Path"
    $idfPython = Join-Path $python "python.exe"
    Write-Host "Installing project-local Espressif QEMU for Xtensa"
    & $idfPython (Join-Path $idf "tools\idf_tools.py") install qemu-xtensa
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 QEMU installation failed."
    }
    Set-Content -LiteralPath $qemuMarker -Value @(
        "ESP-IDF=5.5.5"
        "Target=esp32"
        "Installed=$([DateTime]::UtcNow.ToString('o'))"
    ) -Encoding ascii
} finally {
    $env:Path = $savedPath
    $env:IDF_PATH = $savedIdfPath
    $env:IDF_TOOLS_PATH = $savedIdfToolsPath
    $env:PYTHONNOUSERSITE = $savedPythonNoUserSite
}

Write-Host "Project-local ESP32 QEMU is ready."
