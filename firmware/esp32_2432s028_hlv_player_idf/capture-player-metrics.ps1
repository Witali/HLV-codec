[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateRange(1, 1000000)][int]$Frames = 900,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 120,
    [string]$OutputCsv,
    [switch]$AllowAudioUnderrun
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$tools = Join-Path $project ".tools"
$pythonEnvironments = Join-Path $tools "espressif\python_env"

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

$captureArguments = @(
    (Join-Path $project "capture_player_metrics.py"),
    "--port", $Port,
    "--frames", $Frames,
    "--timeout", $TimeoutSeconds,
    "--reset"
)
if ($AllowAudioUnderrun) {
    $captureArguments += "--allow-audio-underrun"
}
if ($OutputCsv) {
    $captureArguments += @("--output-csv", $OutputCsv)
}

& $python @captureArguments
if ($LASTEXITCODE -ne 0) {
    throw "Player metric capture failed with exit code $LASTEXITCODE"
}
