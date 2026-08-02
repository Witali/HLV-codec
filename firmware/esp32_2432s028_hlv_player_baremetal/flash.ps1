[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build.ps1")
}
$buildDirectory = Join-Path $PSScriptRoot "build"
if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory "flash_args"))) {
    throw "Firmware is not built: missing $buildDirectory\flash_args"
}

$env:ESPTOOL_OPEN_PORT_ATTEMPTS = "60"
& (Join-Path $PSScriptRoot "idf.ps1") `
    -EsptoolWorkingDirectory $buildDirectory -EsptoolArguments @(
        "--chip", "esp32", "--port", $Port, "--baud", $Baud.ToString(),
        "--before", "default_reset", "--after", "hard_reset",
        "write_flash", "@flash_args"
    )
