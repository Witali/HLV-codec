[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$idfScript = Join-Path $project "idf.ps1"
if (-not $SkipBuild) {
    & (Join-Path $project "build.ps1")
}

$buildDirectory = Join-Path $project "build"
$flashArguments = Join-Path $buildDirectory "flash_args"
if (-not (Test-Path -LiteralPath $flashArguments)) {
    throw "Firmware is not built: missing $flashArguments"
}

Write-Host "Entering the ROM downloader automatically on $Port..."
$env:ESPTOOL_OPEN_PORT_ATTEMPTS = "60"
& $idfScript -EsptoolWorkingDirectory $buildDirectory -EsptoolArguments @(
    "--chip", "esp32",
    "--port", $Port,
    "--baud", $Baud.ToString(),
    "--before", "default_reset",
    "--after", "hard_reset",
    "write_flash", "@flash_args"
)
