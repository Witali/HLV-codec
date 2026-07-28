[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$Json
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

$arguments = @(
    (Join-Path $project "uart_list.py"),
    "--port", $Port
)
if ($Json) {
    $arguments += "--json"
}
& $python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "UART file listing failed with exit code $LASTEXITCODE"
}
