[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "setup.ps1")
if ($Clean) {
    & (Join-Path $PSScriptRoot "idf.ps1") -IdfArguments @("fullclean")
}
& (Join-Path $PSScriptRoot "idf.ps1") -IdfArguments @("build")
Write-Host "Bare-metal-style firmware is ready in $(
    Join-Path $PSScriptRoot 'build')"
