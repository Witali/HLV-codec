[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$FlashImage,
    [string]$SdImage = (
        Join-Path $PSScriptRoot "qemu\hlv-big-buck-bunny-5min-h263-avi.img"
    ),
    [string]$QemuRoot,
    [switch]$SkipSetup,
    [switch]$Headless,
    [ValidateRange(0, 100)]
    [int]$Volume = 70
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
if (-not $QemuRoot) {
    $QemuRoot = if ($env:HLV_QEMU_ESP32_ROOT) {
        $env:HLV_QEMU_ESP32_ROOT
    } else {
        Join-Path $repository "..\QEMU-ESP32"
    }
}
$QemuRoot = [IO.Path]::GetFullPath($QemuRoot)
$qemu = Join-Path $QemuRoot "bin\qemu-system-xtensa.exe"
$qemuData = Join-Path $QemuRoot "share\qemu"
$qemuSetup = Join-Path $QemuRoot "setup-qemu-esp32-windows.ps1"
$flash = [IO.Path]::GetFullPath($FlashImage)
$sd = [IO.Path]::GetFullPath($SdImage)

if (-not $SkipSetup) {
    if (-not (Test-Path -LiteralPath $qemuSetup -PathType Leaf)) {
        throw "QEMU-ESP32 setup script does not exist: $qemuSetup"
    }
    & $qemuSetup
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU-ESP32 setup failed with code $LASTEXITCODE."
    }
}
foreach ($image in @($flash, $sd)) {
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw "Image does not exist: $image"
    }
}
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) {
    throw "QEMU-ESP32 runtime does not exist: $qemu"
}
if (-not (Test-Path -LiteralPath (
    Join-Path $qemuData "esp32-v3-rom.bin"
) -PathType Leaf)) {
    throw "ESP32 QEMU ROM data does not exist: $qemuData"
}

$display = if ($Headless) { "none" } else { "sdl" }
& $qemu @(
    "-L", $qemuData,
    "-accel", "tcg,thread=multi",
    "-machine", (
        "esp32,sdspi=on,st7789=on,audiodev=esp32dac," +
        "dac-rate=8000,dac-volume=$Volume"
    ),
    "-audiodev", "dsound,id=esp32dac",
    "-display", $display,
    "-monitor", "none",
    "-serial", "stdio",
    "-drive", "file=$flash,if=mtd,format=raw",
    "-drive", "file=$sd,if=sd,format=raw"
)
if ($LASTEXITCODE -ne 0) {
    throw "QEMU exited with code $LASTEXITCODE."
}
