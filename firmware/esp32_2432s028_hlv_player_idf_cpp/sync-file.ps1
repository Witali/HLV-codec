[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string]$Name,
    [ValidateScript({ $_ -ge 4096 -and $_ -le 1048576 -and ($_ -band ($_ - 1)) -eq 0 })]
    [UInt32]$BlockSize = 65536,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [UInt32]$DataBaud = 1000000,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$primary = Join-Path $PSScriptRoot "..\esp32_2432s028_hlv_player_idf_c\sync-file.ps1"
& $primary @PSBoundParameters
