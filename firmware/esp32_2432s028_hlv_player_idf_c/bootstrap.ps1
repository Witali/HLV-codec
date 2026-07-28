[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
& (Join-Path $PSScriptRoot "setup.ps1")
