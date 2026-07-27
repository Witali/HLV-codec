[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 90)]
    [int]$Frames = 60
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

& (Join-Path $project "prepare-qemu-mpeg1-benchmark.ps1") `
    -InputFile $InputFile -Frames $Frames
& (Join-Path $project "setup-qemu.ps1")
& (Join-Path $project "idf.ps1") -IdfArguments @(
    "-B", "build-qemu-mpeg1",
    "-D", "MPEG1_QEMU_BENCHMARK=ON",
    "qemu",
    "--qemu-extra-args=-no-reboot -icount shift=0,sleep=off"
)
