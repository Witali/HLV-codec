[CmdletBinding()]
param(
    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$toolSource = Join-Path $repo "tools\custom_audio_ima.c"
$hlvSource = Join-Path $repo "codecs\hlv\src\hlv1_common.c"
$imaSource = Join-Path $repo "codecs\common\src\ima_adpcm.c"
$hlvInclude = Join-Path $repo "codecs\hlv\include"
$commonInclude = Join-Path $repo "codecs\common\include"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer (vswhere.exe) was not found."
}
$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vs) {
    throw "Visual Studio C/C++ tools are not installed."
}
$devcmd = Join-Path $vs "Common7\Tools\VsDevCmd.bat"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$output = Join-Path $OutputDirectory "custom_audio_ima.exe"
$commandTemplate = 'call "{0}" -no_logo -arch=x64 && ' +
    'cl /nologo /O2 /W4 /std:c11 /D_CRT_SECURE_NO_WARNINGS ' +
    '/I"{1}" /I"{2}" "{3}" "{4}" "{5}" /Fe:"{6}"'
$command = $commandTemplate -f $devcmd, $hlvInclude, $commonInclude, `
    $toolSource, $hlvSource, $imaSource, $output

Write-Host "Building the streaming HLV/BPV audio rewriter..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building custom_audio_ima."
}
Write-Host "Ready: $output"
