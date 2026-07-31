[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][ValidateSet(
        460800, 921600, 1000000, 1500000, 2000000, 3000000
    )][UInt32]$ToBaud,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [UInt32]$FromBaud = 1000000
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

& $python (Join-Path $project "uart_baud.py") `
    --port $Port --from-baud $FromBaud --to-baud $ToBaud
if ($LASTEXITCODE -ne 0) {
    throw "UART baud change failed with exit code $LASTEXITCODE"
}
