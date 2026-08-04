[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 60)]
    [int]$Frames = 12,
    [ValidateSet("ON", "OFF")]
    [string]$StreamingInput = "ON",
    [ValidateRange(1024, 65536)]
    [int]$InputBufferBytes = 8192,
    [ValidateSet("ON", "OFF")]
    [string]$HotIram = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$OptimizedIdct = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$ReducedIdct = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$FastClear = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$FixedRgb565 = "ON",
    [ValidateSet("ON", "OFF")]
    [string]$ColorTables = "ON"
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
    "-D", "SDKCONFIG=$project\build-qemu-mjpeg\sdkconfig.qemu",
    "-D", "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.qemu.defaults",
    "-D", "MJPEG_QEMU_BENCHMARK=ON",
    "-D", "MJPEG_QEMU_FRAME_LIMIT=$Frames",
    "-D", "MJPEG_STREAMING_INPUT=$StreamingInput",
    "-D", "MJPEG_INPUT_BUFFER_BYTES=$InputBufferBytes",
    "-D", "MJPEG_HOT_IRAM=$HotIram",
    "-D", "MJPEG_OPTIMIZED_IDCT=$OptimizedIdct",
    "-D", "MJPEG_IDCT_REDUCED_ROWS=$ReducedIdct",
    "-D", "MJPEG_FAST_COEFFICIENT_CLEAR=$FastClear",
    "-D", "MJPEG_FIXED_RGB565=$FixedRgb565",
    "-D", "MJPEG_COLOR_TABLES=$ColorTables",
    "qemu",
    "--qemu-extra-args=-no-reboot -icount shift=0,sleep=off"
)
