[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$FlashImage,
    [Parameter(Mandatory)]
    [string]$SdImage,
    [switch]$SkipSetup,
    [switch]$Headless
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$qemuRoot = Join-Path $repository "local_tools\qemu-sdspi-windows"
$qemu = Join-Path $qemuRoot "bin\qemu-system-xtensa.exe"
$qemuData = Join-Path $qemuRoot "share\qemu"
$flash = [IO.Path]::GetFullPath($FlashImage)
$sd = [IO.Path]::GetFullPath($SdImage)

if (-not $SkipSetup) {
    & (Join-Path $project "setup-qemu-sdspi-windows.ps1")
}
foreach ($image in @($flash, $sd)) {
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw "Image does not exist: $image"
    }
}
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) {
    throw "Native Windows QEMU does not exist: $qemu"
}
if (-not (Test-Path -LiteralPath (
    Join-Path $qemuData "esp32-v3-rom.bin"
) -PathType Leaf)) {
    throw "ESP32 QEMU ROM data does not exist: $qemuData"
}

$display = if ($Headless) { "none" } else { "sdl" }
& $qemu @(
    "-L", $qemuData,
    "-machine", "esp32,sdspi=on,st7789=on",
    "-display", $display,
    "-monitor", "none",
    "-serial", "stdio",
    "-drive", "file=$flash,if=mtd,format=raw",
    "-drive", "file=$sd,if=sd,format=raw"
)
if ($LASTEXITCODE -ne 0) {
    throw "QEMU exited with code $LASTEXITCODE."
}
