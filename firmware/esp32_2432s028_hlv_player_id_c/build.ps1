[CmdletBinding()]
param([switch]$Clean)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot

& (Join-Path $project "setup.ps1")
if ($Clean) {
    & (Join-Path $project "idf.ps1") -IdfArguments @("fullclean")
}
& (Join-Path $project "idf.ps1") -IdfArguments @("build")

Write-Host "ESP-IDF firmware is ready in $(Join-Path $project 'build')"
