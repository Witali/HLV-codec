[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600, 1000000, 1500000,
                 2000000, 3000000)]
    [int]$Baud = 460800,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$idf = Join-Path $project "idf.ps1"
$build = Join-Path $project "build-pdm-tone"

if (-not $SkipBuild) {
    & $idf -IdfArguments @(
        "-B", "build-pdm-tone",
        "-D", "PDM_TONE_TEST=ON",
        "-D", "TONE_TEST=OFF",
        "build"
    )
}

$flashArguments = Join-Path $build "flash_args"
if (-not (Test-Path -LiteralPath $flashArguments -PathType Leaf)) {
    throw "PDM tone firmware is not built: missing $flashArguments"
}

$savedOpenAttempts = $env:ESPTOOL_OPEN_PORT_ATTEMPTS
try {
    $env:ESPTOOL_OPEN_PORT_ATTEMPTS = "60"
    Write-Host "Flashing the SD-free I2S PDM sine-ramp image on $Port..."
    & $idf -EsptoolWorkingDirectory $build -EsptoolArguments @(
        "--chip", "esp32",
        "--port", $Port,
        "--baud", $Baud.ToString(),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash", "@flash_args"
    )
} finally {
    $env:ESPTOOL_OPEN_PORT_ATTEMPTS = $savedOpenAttempts
}
