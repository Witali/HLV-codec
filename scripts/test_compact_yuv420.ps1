[CmdletBinding()]
param(
    [string]$OutputDirectory = "build\compact-yuv420-test"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$include = Join-Path $repo "codecs\common\include"
$source = Join-Path $repo "codecs\common\tests\test_compact_yuv420.c"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "test_compact_yuv420.exe"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /TC /I"{2}" "{3}" /Fe:"{4}"'
) -f $devcmd, $OutputDirectory, $include, $source, $output
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building compact YUV420 tests."
}

& $output
if ($LASTEXITCODE -ne 0) {
    throw "Compact YUV420 tests failed."
}
