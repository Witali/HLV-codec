[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
& (Join-Path $PSScriptRoot "bootstrap_arduino.ps1")
