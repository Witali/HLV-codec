[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$PythonArguments
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$python = Join-Path $repo "local_tools\python\python.exe"
& (Join-Path $PSScriptRoot "setup_python.ps1")

$savedPath = $env:Path
try {
    $env:Path = "$(Join-Path $repo 'tools\ffmpeg\bin');$(Join-Path $repo 'build\msvc');$env:Path"
    & $python @PythonArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Python failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:Path = $savedPath
}
