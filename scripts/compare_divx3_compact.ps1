[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) `
            "build\divx3-compact-comparison"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$InputFile = [IO.Path]::GetFullPath($InputFile)
if (-not (Test-Path -LiteralPath $InputFile)) {
    throw "DivX 3 input is missing: $InputFile"
}
$vswhere =
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1
if (-not $installation) {
    throw "Visual Studio C++ tools are missing."
}

$devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
$divx = Join-Path $repo "codecs\divx3"
$compactInclude = Join-Path $repo "codecs\common\include"
$testSource = Join-Path $divx "tests\compare_compact.c"
$decoderSource = Join-Path $divx "src\divx3_decode.c"
$aviSource = Join-Path $divx "src\divx3_avi.c"
$imaSource = Join-Path $repo "codecs\common\src\ima_adpcm.c"
$demuxSource = Join-Path $repo "codecs\common\src\avi_demux.c"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "compare_divx3_compact.exe"
$command = (
    'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
    'cl /nologo /O2 /W4 /TC /D_CRT_SECURE_NO_WARNINGS ' +
    '/I"{2}" /I"{3}" "{4}" "{5}" "{6}" "{7}" "{8}" /Fe:"{9}"'
) -f $devcmd, $OutputDirectory, (Join-Path $divx "include"), `
    $compactInclude, $testSource, $decoderSource, $aviSource, $imaSource, `
    $demuxSource, $output
& cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building the DivX 3 comparison."
}

& $output $InputFile
if ($LASTEXITCODE -ne 0) {
    throw "DivX 3 compact comparison failed."
}
