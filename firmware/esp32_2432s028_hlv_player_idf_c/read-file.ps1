[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string]$Output,
    [UInt32]$Offset = 0,
    [UInt32]$Length,
    [ValidateSet(460800, 921600, 1000000, 1500000, 2000000, 3000000)]
    [UInt32]$DataBaud = 460800,
    [switch]$Force
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
    (Join-Path $project "uart_read.py"),
    "--port", $Port,
    "--offset", $Offset,
    "--data-baud", $DataBaud,
    $Name,
    $Output
)
if ($PSBoundParameters.ContainsKey("Length")) {
    $arguments = @(
        (Join-Path $project "uart_read.py"),
        "--port", $Port,
        "--offset", $Offset,
        "--length", $Length,
        "--data-baud", $DataBaud,
        $Name,
        $Output
    )
}
if ($Force) {
    $arguments += "--force"
}
& $python @arguments
if ($LASTEXITCODE -ne 0) {
    throw "UART file read failed with exit code $LASTEXITCODE"
}
