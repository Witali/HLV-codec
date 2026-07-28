[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$All,
    [switch]$Json
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$toolProject = (
    Resolve-Path (
        Join-Path $project "..\esp32_2432s028_hlv_player_idf_c"
    )
).Path
$pythonEnvironments =
    Join-Path $toolProject ".tools\espressif\python_env"

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

$arguments = @(
    (Join-Path $project "uart_crc.py"),
    "--port", $Port
)
if ($All) {
    $arguments += "--all"
}
if ($Json) {
    $arguments += "--json"
}
& $python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "UART file checksum failed with exit code $LASTEXITCODE"
}
