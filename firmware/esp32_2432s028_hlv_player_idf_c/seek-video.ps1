[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][ValidateRange(0, 4294967295)][UInt32]$Milliseconds,
    [ValidateSet(115200, 230400, 460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [int]$Baud = 460800,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 600
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$pythonEnvironments = Join-Path $project ".tools\espressif\python_env"
if (-not (Test-Path -LiteralPath $pythonEnvironments)) {
    & (Join-Path $project "setup.ps1")
}
$python = Get-ChildItem -LiteralPath $pythonEnvironments -Recurse `
    -Filter python.exe |
    Where-Object { $_.FullName -like "*\Scripts\python.exe" } |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $python) {
    throw "Project-local ESP-IDF Python environment was not found."
}

& $python (Join-Path $project "capture_player_metrics.py") `
    --port $Port --baud $Baud --frames 1 --timeout $TimeoutSeconds `
    --seek-ms $Milliseconds --allow-audio-underrun
if ($LASTEXITCODE -ne 0) {
    throw "Video seek failed with exit code $LASTEXITCODE"
}
