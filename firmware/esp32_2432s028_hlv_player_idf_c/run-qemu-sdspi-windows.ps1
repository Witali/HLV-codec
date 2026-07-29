[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$FlashImage,
    [string]$SdImage = (
        Join-Path $PSScriptRoot "qemu\hlv-big-buck-bunny-5min-h263-avi.img"
    ),
    [switch]$SkipSetup,
    [switch]$Headless,
    [ValidateRange(0, 100)]
    [int]$Volume = 70
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$qemuRoot = Join-Path $repository "local_tools\qemu-sdspi-windows"
$qemu = Join-Path $qemuRoot "bin\qemu-system-xtensa.exe"
$qemuData = Join-Path $qemuRoot "share\qemu"
$flash = [IO.Path]::GetFullPath($FlashImage)
$sd = [IO.Path]::GetFullPath($SdImage)

if (-not $SkipSetup -and
    -not (Test-Path -LiteralPath $qemu -PathType Leaf)) {
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
