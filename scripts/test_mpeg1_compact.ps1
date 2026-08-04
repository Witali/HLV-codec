[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\mpeg1-tests"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$InputFile = [IO.Path]::GetFullPath($InputFile)
if (-not (Test-Path -LiteralPath $InputFile)) {
    throw "MPEG test input is missing: $InputFile"
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
$testSource = Join-Path $repo "codecs\mpeg1\tests\test_decode.c"
$decoderSource = Join-Path $repo "codecs\mpeg1\src\pl_mpeg.c"
$include = Join-Path $repo "third_party\pl_mpeg"
$compactInclude = Join-Path $repo "codecs\common\include"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Build-DecoderTest {
    param(
        [Parameter(Mandatory)]
        [string]$Name,
        [string]$Defines = ""
    )
    $output = Join-Path $OutputDirectory "$Name.exe"
    $command = (
        'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
        'cl /nologo /O2 /W4 /TC /D_CRT_SECURE_NO_WARNINGS {2} ' +
        '/I"{3}" /I"{4}" "{5}" "{6}" /Fe:"{7}"'
    ) -f $devcmd, $OutputDirectory, $Defines, $include, $compactInclude, `
        $testSource, $decoderSource, $output
    & cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        throw "MSVC failed while building $Name."
    }
    return $output
}

$plain = Build-DecoderTest -Name "mpeg1_decode_plain"
$compact = Build-DecoderTest -Name "mpeg1_decode_compact" `
    -Defines "/DPLM_MPEG_EMBEDDED"
$bounded = Build-DecoderTest -Name "mpeg1_decode_bounded" `
    -Defines "/DPLM_MPEG_EMBEDDED /DPLM_MPEG_STREAM_B_ROWS=1"

$plainResult = & $plain $InputFile plain
if ($LASTEXITCODE -ne 0) {
    throw "Plain MPEG decoder regression failed."
}
$compactResult = & $compact $InputFile compact
if ($LASTEXITCODE -ne 0) {
    throw "Compact MPEG decoder regression failed."
}
$boundedResult = & $bounded $InputFile bounded
if ($LASTEXITCODE -ne 0) {
    throw "Bounded MPEG B-row decoder regression failed."
}

$plainFrames = [regex]::Match($plainResult, "frames=(\d+)").Groups[1].Value
$compactFrames =
    [regex]::Match($compactResult, "frames=(\d+)").Groups[1].Value
$boundedFrames =
    [regex]::Match($boundedResult, "frames=(\d+)").Groups[1].Value
if (-not $plainFrames -or $plainFrames -ne $compactFrames -or
    $plainFrames -ne $boundedFrames) {
    throw (
        "Frame count mismatch: plain=$plainFrames compact=$compactFrames " +
        "bounded=$boundedFrames"
    )
}

$plainTypes = [regex]::Match($plainResult, "i=(\d+) p=(\d+) b=(\d+)").Value
$compactTypes =
    [regex]::Match($compactResult, "i=(\d+) p=(\d+) b=(\d+)").Value
$boundedTypes =
    [regex]::Match($boundedResult, "i=(\d+) p=(\d+) b=(\d+)").Value
if (-not $plainTypes -or $plainTypes -ne $compactTypes -or
    $plainTypes -ne $boundedTypes) {
    throw (
        "Picture-type mismatch: plain='$plainTypes' " +
        "compact='$compactTypes' bounded='$boundedTypes'"
    )
}

Write-Host $plainResult
Write-Host $compactResult
Write-Host $boundedResult
Write-Host "MPEG compact/bounded regression passed: $boundedFrames frames."
