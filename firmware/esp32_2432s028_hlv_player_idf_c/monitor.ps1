[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600, 1000000, 1500000, 2000000, 3000000)][int]$Baud = 1000000
)

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "idf.ps1") -IdfArguments @(
    "-p", $Port, "-b", $Baud.ToString(), "monitor"
)
