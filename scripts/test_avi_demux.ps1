[CmdletBinding()]
param(
    [string]$OutputDirectory = "build\avi-demux-test",
    [string[]]$InputPath
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) { throw "Visual Studio C++ tools are missing." }

if (-not $InputPath) {
    $InputPath = @(Get-ChildItem -LiteralPath (Join-Path $repo "out") `
        -Recurse -File -Filter "*.avi" | ForEach-Object { $_.FullName })
}
if (-not $InputPath -or $InputPath.Count -eq 0) {
    throw "No AVI files were found for the demux regression."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$include = Join-Path $repo "codecs\common\include"
$testSource = Join-Path $repo "codecs\common\tests\test_avi_demux.c"
$demuxSource = Join-Path $repo "codecs\common\src\avi_demux.c"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "test_avi_demux.exe"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /TC /I"{2}" ' +
    '"{3}" "{4}" /Fe:"{5}"'
) -f $devcmd, $OutputDirectory, $include, $testSource, $demuxSource, $output
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw "MSVC failed while building AVI demux tests." }

& $output @InputPath
if ($LASTEXITCODE -ne 0) { throw "Common AVI demux regression failed." }
Write-Host "Common AVI demux passed for $($InputPath.Count) files."
