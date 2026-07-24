[CmdletBinding()]
param(
    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"
    )
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "codecs\bpv\tools\bpv1enc.c"
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
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$output = Join-Path $OutputDirectory "bpv1enc.exe"
$object = Join-Path $OutputDirectory "bpv1enc.obj"

$commandTemplate = 'call "{0}" -no_logo -arch=x64 && ' +
    'cl /nologo /O2 /W4 /std:c11 ' +
    '"{1}" /Fo:"{2}" /Fe:"{3}"'
$command = $commandTemplate -f $devcmd, $source, $object, $output

Write-Host "Building BPV1 C encoder..."
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "MSVC failed while building bpv1enc."
}

$previousEncoder = $env:BPV1ENC
try {
    $env:BPV1ENC = $output
    & node (Join-Path $repo "codecs\bpv\tests\bpv1-c-encoder.test.js")
    if ($LASTEXITCODE -ne 0) {
        throw "BPV1 C encoder compatibility test failed."
    }
}
finally {
    $env:BPV1ENC = $previousEncoder
}

Write-Host "BPV1 C encoder is ready: $output"
