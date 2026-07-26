[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 60)]
    [int]$Frames = 12,
    [ValidateSet("ON", "OFF")]
    [string]$HotIram = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$OptimizedIdct = "ON"
)

$ErrorActionPreference = "Stop"

# The optimized wrapper lives in IRAM. Preserve the historical -HotIram OFF
# command as an all-Flash control unless an IDCT choice was explicit.
if ($HotIram -eq "OFF" -and
    -not $PSBoundParameters.ContainsKey("OptimizedIdct")) {
    $OptimizedIdct = "OFF"
}

$project = $PSScriptRoot

& (Join-Path $project "prepare-qemu-mjpeg-benchmark.ps1") `
    -InputFile $InputFile -Frames $Frames
& (Join-Path $project "setup-qemu.ps1")
& (Join-Path $project "idf.ps1") -IdfArguments @(
    "-B", "build-qemu-mjpeg",
    "-D", "MJPEG_QEMU_BENCHMARK=ON",
    "-D", "MJPEG_QEMU_FRAME_LIMIT=$Frames",
    "-D", "MJPEG_HOT_IRAM=$HotIram",
    "-D", "MJPEG_OPTIMIZED_IDCT=$OptimizedIdct",
    "qemu",
    "--qemu-extra-args=-no-reboot -icount shift=0,sleep=off"
)
