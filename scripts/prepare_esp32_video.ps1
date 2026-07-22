[CmdletBinding()]
param(
    [string]$InputFile,
    [string]$OutputFile = (Join-Path (Split-Path $PSScriptRoot -Parent) "out\video.hlv"),
    [ValidateRange(1, 30)][int]$Fps = 15,
    [ValidateRange(1, 100)][int]$Quality = 45,
    [ValidateRange(1, 120)][int]$Duration = 10
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"
$ffmpeg = Join-Path $repo "tools\ffmpeg\bin\ffmpeg.exe"

if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $encoder)) {
    & (Join-Path $PSScriptRoot "build_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $encoder)) {
    throw "hlvenc.exe was not built."
}

$outputParent = Split-Path $OutputFile -Parent
if ($outputParent) {
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
}
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$temporaryY4m = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv1-esp32-{0}.y4m" -f [guid]::NewGuid().ToString("N"))

try {
    $filter = "fps=$Fps,scale=320:240:force_original_aspect_ratio=decrease:" +
        "flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,format=yuv420p"

    if ($InputFile) {
        $InputFile = (Resolve-Path -LiteralPath $InputFile).Path
        & $ffmpeg -y -hide_banner -loglevel error -i $InputFile -an `
            -vf $filter -f yuv4mpegpipe $temporaryY4m
    } else {
        & $ffmpeg -y -hide_banner -loglevel error -f lavfi `
            -i "testsrc2=size=320x240:rate=${Fps}:duration=$Duration" -an `
            -vf "format=yuv420p" -f yuv4mpegpipe $temporaryY4m
    }
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg conversion failed." }

    & $encoder $temporaryY4m $OutputFile --preset balanced `
        --quality $Quality --gop 30 --syntax 12
    if ($LASTEXITCODE -ne 0) { throw "HLV-1 encoding failed." }

    $size = (Get-Item -LiteralPath $OutputFile).Length
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f $OutputFile, $size)
    Write-Host "build_esp32.ps1 will package it into the internal LittleFS image."
} finally {
    if (Test-Path -LiteralPath $temporaryY4m) {
        Remove-Item -LiteralPath $temporaryY4m -Force
    }
}
