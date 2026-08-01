[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [ValidateSet(115200, 230400, 460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [int]$Baud = 460800,
    [ValidateRange(1, 1000000)][int]$Frames = 900,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 120,
    [string]$OutputCsv,
    [Nullable[UInt32]]$SeekMilliseconds,
    [switch]$AllowAudioUnderrun
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$toolProject = (
    Resolve-Path (
        Join-Path $project "..\esp32_2432s028_hlv_player_idf_c"
    )
).Path
$tools = Join-Path $toolProject ".tools"
$pythonEnvironments = Join-Path $tools "espressif\python_env"

if (-not (Test-Path -LiteralPath $pythonEnvironments)) {
    & (Join-Path $toolProject "setup.ps1")
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
    "--baud", $Baud,
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
if ($null -ne $SeekMilliseconds) {
    $captureArguments += @("--seek-ms", $SeekMilliseconds)
}

& $python @captureArguments
if ($LASTEXITCODE -ne 0) {
    throw "Player metric capture failed with exit code $LASTEXITCODE"
}
