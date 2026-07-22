[CmdletBinding()]
param(
    [string]$InputFile = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) "out\video.hlv"),
    [string]$OutputFile = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) "build\esp32\littlefs.bin")
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$fileSystemSize = 0x160000
$mklittlefsRoot = Join-Path $repo `
    "local_tools\arduino\data\packages\esp32\tools\mklittlefs"
$mklittlefs = Get-ChildItem -LiteralPath $mklittlefsRoot -Recurse `
    -Filter mklittlefs.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $mklittlefs) {
    & (Join-Path $PSScriptRoot "bootstrap_arduino.ps1")
    $mklittlefs = Get-ChildItem -LiteralPath $mklittlefsRoot -Recurse `
        -Filter mklittlefs.exe | Select-Object -First 1 -ExpandProperty FullName
}

$InputFile = (Resolve-Path -LiteralPath $InputFile).Path
$inputSize = (Get-Item -LiteralPath $InputFile).Length
if ($inputSize -ge ($fileSystemSize - 16384)) {
    throw "video.hlv is too large for the 0x160000-byte LittleFS partition."
}

$outputParent = Split-Path $OutputFile -Parent
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$staging = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv1-littlefs-{0}" -f [guid]::NewGuid().ToString("N"))

try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    Copy-Item -LiteralPath $InputFile `
        -Destination (Join-Path $staging "video.hlv")
    & $mklittlefs -c $staging -p 256 -b 4096 -s $fileSystemSize $OutputFile
    if ($LASTEXITCODE -ne 0) { throw "mklittlefs failed." }
} finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

$imageSize = (Get-Item -LiteralPath $OutputFile).Length
Write-Host "LittleFS image is ready: $OutputFile ($imageSize bytes)"
