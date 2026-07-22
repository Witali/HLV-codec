[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
if (-not $SkipBuild) {
    & (Join-Path $project "build.ps1")
}

Write-Host "Hold BOOT, tap RST, release BOOT; waiting on $Port..."
$env:ESPTOOL_OPEN_PORT_ATTEMPTS = "60"
& (Join-Path $project "idf.ps1") -IdfArguments @(
    "-p", $Port, "-b", $Baud.ToString(), "flash"
)
