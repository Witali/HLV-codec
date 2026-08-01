[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string]$Name,
    [ValidateScript({ $_ -ge 4096 -and $_ -le 1048576 -and ($_ -band ($_ - 1)) -eq 0 })]
    [UInt32]$BlockSize = 65536,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [UInt32]$DataBaud = 460800,
    [switch]$DryRun
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
    (Join-Path $project "uart_sync.py"),
    (Resolve-Path -LiteralPath $File).Path,
    "--port", $Port,
    "--name", $Name,
    "--block-size", $BlockSize,
    "--data-baud", $DataBaud
)
if ($DryRun) {
    $arguments += "--dry-run"
}
& $python $arguments
if ($LASTEXITCODE -ne 0) {
    throw "UART block synchronization failed with exit code $LASTEXITCODE."
}
