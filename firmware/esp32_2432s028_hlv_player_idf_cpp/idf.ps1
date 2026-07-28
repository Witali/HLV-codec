[CmdletBinding(DefaultParameterSetName = "Idf")]
param(
    [Parameter(ParameterSetName = "Idf", ValueFromRemainingArguments = $true)]
    [string[]]$IdfArguments,
    [Parameter(Mandatory, ParameterSetName = "Esptool")]
    [string[]]$EsptoolArguments,
    [Parameter(ParameterSetName = "Esptool")]
    [string]$EsptoolWorkingDirectory
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$toolProject = (
    Resolve-Path (
        Join-Path $project "..\esp32_2432s028_hlv_player_idf_c"
    )
).Path
$tools = Join-Path $toolProject ".tools"
$idf = Join-Path $tools "esp-idf-v5.5.5"
$idfTools = Join-Path $tools "espressif"
$python = Join-Path $tools "python"
$marker = Join-Path $tools "ready-v5.5.5"

if (-not (Test-Path -LiteralPath (Join-Path $idf "tools\idf.py")) -or
    -not (Test-Path -LiteralPath $marker)) {
    & (Join-Path $toolProject "setup.ps1")
}

$savedPath = $env:Path
$savedIdfPath = $env:IDF_PATH
$savedIdfToolsPath = $env:IDF_TOOLS_PATH
$savedPythonNoUserSite = $env:PYTHONNOUSERSITE
$savedEsptoolConfig = $env:ESPTOOL_CFGFILE
try {
    $env:IDF_TOOLS_PATH = $idfTools
    $env:PYTHONNOUSERSITE = "1"
    $env:ESPTOOL_CFGFILE = Join-Path $project "esptool.cfg"
    $env:Path = "$python;$python\Scripts;$env:Path"
    . (Join-Path $idf "export.ps1")
    $idfPython = (Get-Command python.exe -CommandType Application |
        Select-Object -First 1 -ExpandProperty Source)
    $workingDirectory = if ($PSCmdlet.ParameterSetName -eq "Esptool" -and
        $EsptoolWorkingDirectory) {
        $EsptoolWorkingDirectory
    } else {
        $project
    }
    Push-Location $workingDirectory
    try {
        if ($PSCmdlet.ParameterSetName -eq "Esptool") {
            & $idfPython -m esptool @EsptoolArguments
            $toolName = "esptool"
        } else {
            & $idfPython (Join-Path $idf "tools\idf.py") @IdfArguments
            $toolName = "idf.py"
        }
        if ($LASTEXITCODE -ne 0) {
            throw "$toolName failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:Path = $savedPath
    $env:IDF_PATH = $savedIdfPath
    $env:IDF_TOOLS_PATH = $savedIdfToolsPath
    $env:PYTHONNOUSERSITE = $savedPythonNoUserSite
    $env:ESPTOOL_CFGFILE = $savedEsptoolConfig
}
