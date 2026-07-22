[CmdletBinding()]
param(
    [ValidateSet("O2", "O3")]
    [string]$Optimization = "O2"
)

$ErrorActionPreference = "Stop"
$project = Split-Path $PSScriptRoot -Parent
$repo = (Resolve-Path (Join-Path $project "..\..")).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing. Run $repo\setup.ps1 first."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$component = Join-Path $project "components\hlv1"
$include = Join-Path $component "include"
$privateInclude = Join-Path $component "src"
$common = Join-Path $privateInclude "hlv1_common.c"
$decoder = Join-Path $privateInclude "hlv1_decode.c"
$simulator = Join-Path $PSScriptRoot "hlv_esp32_sim.c"
$build = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $build | Out-Null
$output = Join-Path $build "hlv_esp32_sim.exe"

$optimizationFlag = if ($Optimization -eq "O3") { "/Ox" } else { "/O2" }
$commandTemplate = 'call "{0}" -no_logo -arch=x64 && ' +
    'cl /nologo {1} /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS ' +
    '/DHLV1_ESP32_SIMULATOR=1 /DHLV1_ENABLE_DECODER_STATS=0 ' +
    '/I"{2}" /I"{3}" ' +
    '"{4}" "{5}" "{6}" /Fe:"{7}"'
$command = $commandTemplate -f $devcmd, $optimizationFlag, $include,
    $privateInclude, $simulator, $common, $decoder, $output

Write-Host "Building ESP32 decoder simulator ($Optimization)..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building the ESP32 decoder simulator."
}
Write-Host "ESP32 decoder simulator is ready: $output"
