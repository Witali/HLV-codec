[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 120)]
    [int]$Frames = 120,
    [ValidateRange(1, 32)]
    [int]$Windows = 4,
    [ValidateSet(32, 64)]
    [int]$BitReaderBits = 32
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

& (Join-Path $project "prepare-qemu-benchmark.ps1") `
    -InputFile $InputFile -Frames $Frames -Windows $Windows
& (Join-Path $project "setup-qemu.ps1")
$buildDirectory = "build-qemu-br$BitReaderBits"
& (Join-Path $project "idf.ps1") -IdfArguments @(
    "-B", $buildDirectory,
    "-D", "HLV_QEMU_BENCHMARK=ON",
    "-D", "HLV1_BITREADER_BITS=$BitReaderBits",
    "qemu",
    "--qemu-extra-args=-no-reboot -icount shift=0,sleep=off"
)
