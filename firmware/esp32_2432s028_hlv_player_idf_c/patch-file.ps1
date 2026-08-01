[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][UInt32]$Offset,
    [UInt32]$SourceOffset = 0,
    [UInt32]$Length,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [UInt32]$DataBaud = 460800
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
    (Join-Path $project "uart_patch.py"),
    (Resolve-Path -LiteralPath $File).Path,
    "--port", $Port,
    "--name", $Name,
    "--offset", $Offset,
    "--source-offset", $SourceOffset,
    "--data-baud", $DataBaud
)
if ($PSBoundParameters.ContainsKey("Length")) {
    $arguments += @("--length", $Length)
}
& $python $arguments
if ($LASTEXITCODE -ne 0) {
    throw "UART partial patch failed with exit code $LASTEXITCODE."
}
