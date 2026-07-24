[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [Parameter(Mandatory)][string]$File,
    [string]$Name,
    [ValidateSet(460800, 921600, 1500000, 2000000)]
    [int]$DataBaud = 2000000
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
    & (Join-Path $project "setup.ps1")
    $python = Get-ChildItem -LiteralPath $pythonEnvironments -Recurse `
        -Filter python.exe |
        Where-Object { $_.FullName -like "*\Scripts\python.exe" } |
        Select-Object -First 1 -ExpandProperty FullName
}
if (-not $python) {
    throw "Project-local ESP-IDF Python environment was not found."
}

$source = (Resolve-Path -LiteralPath $File).Path
if (-not $Name) {
    $Name = [IO.Path]::GetFileName($source)
}
& $python (Join-Path $project "uart_upload.py") $source `
    --port $Port --name $Name --data-baud $DataBaud
if ($LASTEXITCODE -ne 0) {
    throw "UART video upload failed with exit code $LASTEXITCODE"
}
