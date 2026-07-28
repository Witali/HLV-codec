[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$primaryProject = (
    Resolve-Path (
        Join-Path $PSScriptRoot "..\esp32_2432s028_hlv_player_idf_c"
    )
).Path
& (Join-Path $primaryProject "setup.ps1")
Write-Host (
    "C++ reference firmware uses the pinned ESP-IDF toolchain from " +
    $primaryProject
)
