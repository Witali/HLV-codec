[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [string]$Name,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000)]
    [int]$DataBaud = 460800
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$project = Join-Path $repo "firmware\esp32_2432s028_hlv_player_idf_c"
if (-not $Name) {
    $Name = [IO.Path]::GetFileName($File)
}
& (Join-Path $project "upload-video.ps1") -Port $Port -File $File `
    -Name $Name -DataBaud $DataBaud
