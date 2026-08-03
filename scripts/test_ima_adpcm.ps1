[CmdletBinding()]
param(
    [string]$OutputDirectory = "build\ima-adpcm-test"
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
$testSource = Join-Path $repo "codecs\common\tests\test_ima_adpcm.c"
$codecSource = Join-Path $repo "codecs\common\src\ima_adpcm.c"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "test_ima_adpcm.exe"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /TC /I"{2}" ' +
    '"{3}" "{4}" /Fe:"{5}"'
) -f $devcmd, $OutputDirectory, $include, $testSource, `
    $codecSource, $output
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building IMA ADPCM tests."
}

& $output
if ($LASTEXITCODE -ne 0) {
    throw "IMA ADPCM tests failed."
}
