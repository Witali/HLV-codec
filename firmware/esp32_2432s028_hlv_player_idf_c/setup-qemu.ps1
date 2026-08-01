[CmdletBinding()]
param(
    [string]$QemuRoot
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
if (-not $QemuRoot) {
    $repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
    $QemuRoot = if ($env:HLV_QEMU_ESP32_ROOT) {
        $env:HLV_QEMU_ESP32_ROOT
    } else {
        Join-Path $repository "..\QEMU-ESP32"
    }
}
$QemuRoot = [IO.Path]::GetFullPath($QemuRoot)
$setup = Join-Path $QemuRoot "setup-qemu-esp32-windows.ps1"
$qemu = Join-Path $QemuRoot "bin\qemu-system-xtensa.exe"
if (-not (Test-Path -LiteralPath $setup -PathType Leaf)) {
    throw "QEMU-ESP32 setup script does not exist: $setup"
}
& $setup
if ($LASTEXITCODE -ne 0) {
    throw "QEMU-ESP32 setup failed with code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) {
    throw "QEMU-ESP32 runtime does not exist: $qemu"
}

Write-Host "External QEMU-ESP32 is ready: $qemu"
