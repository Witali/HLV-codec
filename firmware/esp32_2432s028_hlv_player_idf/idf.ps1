[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArguments
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$tools = Join-Path $project ".tools"
$idf = Join-Path $tools "esp-idf-v5.5.5"
$idfTools = Join-Path $tools "espressif"
$python = Join-Path $tools "python"
$marker = Join-Path $tools "ready-v5.5.5"

if (-not (Test-Path -LiteralPath (Join-Path $idf "tools\idf.py")) -or
    -not (Test-Path -LiteralPath $marker)) {
    & (Join-Path $project "bootstrap.ps1")
}

$savedPath = $env:Path
$savedIdfPath = $env:IDF_PATH
$savedIdfToolsPath = $env:IDF_TOOLS_PATH
$savedPythonNoUserSite = $env:PYTHONNOUSERSITE
try {
    $env:IDF_TOOLS_PATH = $idfTools
    $env:PYTHONNOUSERSITE = "1"
    $env:Path = "$python;$python\Scripts;$env:Path"
    . (Join-Path $idf "export.ps1")
    $idfPython = (Get-Command python.exe -CommandType Application |
        Select-Object -First 1 -ExpandProperty Source)
    Push-Location $project
    try {
        & $idfPython (Join-Path $idf "tools\idf.py") @IdfArguments
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:Path = $savedPath
    $env:IDF_PATH = $savedIdfPath
    $env:IDF_TOOLS_PATH = $savedIdfToolsPath
    $env:PYTHONNOUSERSITE = $savedPythonNoUserSite
}
