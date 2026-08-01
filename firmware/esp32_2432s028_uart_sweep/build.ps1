[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
if ($Clean -and (Test-Path -LiteralPath (Join-Path $project "build"))) {
    & (Join-Path $project "idf.ps1") -IdfArguments @("fullclean")
}
& (Join-Path $project "idf.ps1") -IdfArguments @("build")
Write-Host "UART SWEEP firmware is ready in $(Join-Path $project 'build')"
