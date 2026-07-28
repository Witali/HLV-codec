#requires -Version 7.4

[CmdletBinding()]
param(
    [switch]$Rebuild,
    [switch]$Headless,
    [ValidateRange(0, 100)]
    [int]$Volume = 70
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$build = Join-Path $project "build-qemu-demo"
$flash = Join-Path $build "qemu_demo_flash_4mb.bin"
$idf = Join-Path $project "idf.ps1"

if ($Rebuild -or
    -not (Test-Path -LiteralPath $flash -PathType Leaf)) {
    & $idf -IdfArguments @(
        "-C", $project,
        "-B", $build,
        "build"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 demo firmware build failed."
    }

    & $idf `
        -EsptoolArguments @(
            "--chip", "esp32",
            "merge_bin",
            "-o", $flash,
            "--flash_mode", "dio",
            "--flash_freq", "80m",
            "--flash_size", "4MB",
            "--fill-flash-size", "4MB",
            "0x1000", "bootloader\bootloader.bin",
            "0x8000", "partition_table\partition-table.bin",
            "0x10000", "hlv_esp32_player.bin"
        ) `
        -EsptoolWorkingDirectory $build
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32 demo flash image creation failed."
    }
}

$arguments = @{
    FlashImage = $flash
    Volume = $Volume
}
if ($Headless) {
    $arguments.Headless = $true
}

& (Join-Path $project "run-qemu-sdspi-windows.ps1") @arguments
