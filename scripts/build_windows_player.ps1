[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"),
    [switch]$SkipCompilerCheck
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$hlv = Join-Path $repo "codecs\hlv"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not $SkipCompilerCheck) {
    & (Join-Path $PSScriptRoot "setup_msvc.ps1")
}

$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing. Run .\setup.ps1 first."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$include = Join-Path $hlv "include"
$player = Join-Path $hlv "tools\hlvplay_win32.cpp"
$common = Join-Path $hlv "src\hlv1_common.c"
$decoder = Join-Path $hlv "src\hlv1_decode.c"
$output = Join-Path $OutputDirectory "hlvplay.exe"

$commandTemplate = 'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 ' +
    '/D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ' +
    '/I"{2}" "{3}" "{4}" "{5}" /Fe:"{6}" /link /SUBSYSTEM:WINDOWS'
$command = $commandTemplate -f $devcmd, $OutputDirectory, $include, `
    $player, $common, $decoder, $output

Write-Host "Building hlvplay..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building hlvplay."
}

Write-Host "Windows HLV player is ready: $output"
