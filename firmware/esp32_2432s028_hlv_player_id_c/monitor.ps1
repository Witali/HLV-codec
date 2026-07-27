[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "idf.ps1") -IdfArguments @(
    "-p", $Port, "-b", $Baud.ToString(), "monitor"
)
