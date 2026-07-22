[CmdletBinding()]
param(
    [switch]$ForceDownload,
    [switch]$SkipVisualStudioInstall
)

$ErrorActionPreference = "Stop"
$setup = Join-Path $PSScriptRoot "scripts\setup.ps1"
& $setup -ForceDownload:$ForceDownload `
    -SkipVisualStudioInstall:$SkipVisualStudioInstall
