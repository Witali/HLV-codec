[CmdletBinding()]
param(
    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "build\msvc"
    ),
    [string]$CudaArchitecture = "native"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$hostSource = Join-Path $repo "codecs\bpv\tools\bpv1enc.c"
$cudaSource = Join-Path $repo "codecs\bpv\tools\bpv1_cuda.cu"
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$nvccCommand = Get-Command nvcc -ErrorAction SilentlyContinue

if (-not $nvccCommand) {
    throw "CUDA compiler (nvcc) was not found in PATH."
}
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
$output = Join-Path $OutputDirectory "bpv1enc_cuda.exe"
$hostObject = Join-Path $OutputDirectory "bpv1enc_cuda_host.obj"
$cudaObject = Join-Path $OutputDirectory "bpv1_cuda.obj"
$nvcc = $nvccCommand.Source

$commandTemplate = 'call "{0}" -no_logo -arch=x64 && ' +
    'cl /nologo /O2 /W4 /std:c11 /DBPV1_WITH_CUDA /c ' +
    '"{1}" /Fo:"{2}" && ' +
    '"{3}" -O3 -std=c++17 -arch={4} -DBPV1_WITH_CUDA -c ' +
    '"{5}" -o "{6}" && ' +
    '"{3}" -arch={4} "{2}" "{6}" -o "{7}"'
$command = $commandTemplate -f `
    $devcmd, $hostSource, $hostObject, $nvcc, $CudaArchitecture, `
    $cudaSource, $cudaObject, $output

Write-Host (
    "Building BPV1 CUDA encoder for architecture " +
    "$CudaArchitecture..."
)
& cmd.exe /d /c $command
if ($LASTEXITCODE -ne 0) {
    throw "CUDA/MSVC failed while building bpv1enc_cuda."
}

$previousEncoder = $env:BPV1ENC
try {
    $env:BPV1ENC = $output
    & node (Join-Path $repo "codecs\bpv\tests\bpv1-c-encoder.test.js")
    if ($LASTEXITCODE -ne 0) {
        throw "BPV1 CUDA encoder compatibility test failed."
    }
}
finally {
    $env:BPV1ENC = $previousEncoder
}

Write-Host "BPV1 CUDA encoder is ready: $output"
