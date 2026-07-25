[CmdletBinding()]
param(
    [string]$OutputDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"),
    [switch]$SkipCompilerCheck
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$hlv = Join-Path $repo "codecs\hlv"
$bpv = Join-Path $repo "codecs\bpv"
$mpeg = Join-Path $repo "codecs\mpeg1"
$h263 = Join-Path $repo "codecs\h263"
$pv = Join-Path $h263 "third_party\pv"
$plMpeg = Join-Path $repo "third_party\pl_mpeg"
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
$bpvInclude = Join-Path $bpv "include"
$player = Join-Path $hlv "tools\hlvplay_win32.cpp"
$common = Join-Path $hlv "src\hlv1_common.c"
$decoder = Join-Path $hlv "src\hlv1_decode.c"
$bpvDecoder = Join-Path $bpv "src\bpv1_decode.c"
$mpegDecoder = Join-Path $mpeg "src\pl_mpeg.c"
$h263Sources = @(
    (Join-Path $h263 "src\h263_3gp.cpp")
) + @(Get-ChildItem -LiteralPath (Join-Path $pv "src") -Filter "*.cpp" |
    Sort-Object Name | ForEach-Object { $_.FullName })
$h263SourceArguments = ($h263Sources | ForEach-Object {
    '"{0}"' -f $_
}) -join " "
$output = Join-Path $OutputDirectory "hlvplay.exe"

$commandTemplate = 'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /EHsc /std:c++17 /utf-8 ' +
    '/D_CRT_SECURE_NO_WARNINGS /DUNICODE /D_UNICODE ' +
    '/I"{2}" /I"{3}" /I"{4}" /I"{5}" /I"{6}" /I"{7}" ' +
    '"{8}" "{9}" "{10}" "{11}" "{12}" {13} ' +
    '/Fe:"{14}" /link /SUBSYSTEM:WINDOWS'
$command = $commandTemplate -f $devcmd, $OutputDirectory, $include, `
    $bpvInclude, $plMpeg, (Join-Path $h263 "include"), `
    (Join-Path $pv "include"), (Join-Path $pv "src"), $player, $common, `
    $decoder, $bpvDecoder, $mpegDecoder, $h263SourceArguments, $output

Write-Host "Building hlvplay..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building hlvplay."
}

Write-Host "Windows HLV/BPV/MPEG-1/H.263 player is ready: $output"
