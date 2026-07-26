[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 60)]
    [int]$Frames = 12,
    [ValidateSet("ON", "OFF")]
    [string]$HotIram = "ON"
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

& (Join-Path $project "prepare-qemu-mjpeg-benchmark.ps1") `
    -InputFile $InputFile -Frames $Frames
& (Join-Path $project "setup-qemu.ps1")
& (Join-Path $project "idf.ps1") -IdfArguments @(
    "-B", "build-qemu-mjpeg",
    "-D", "MJPEG_QEMU_BENCHMARK=ON",
    "-D", "MJPEG_HOT_IRAM=$HotIram",
    "qemu",
    "--qemu-extra-args=-no-reboot -icount shift=0,sleep=off"
)
