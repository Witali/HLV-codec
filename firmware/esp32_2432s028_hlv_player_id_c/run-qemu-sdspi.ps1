[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$FlashImage,
    [Parameter(Mandatory)]
    [string]$SdImage,
    [switch]$SkipSetup
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$qemu = Join-Path $repository "local_tools\qemu-sdspi\build\qemu-system-xtensa"
$flash = [IO.Path]::GetFullPath($FlashImage)
$sd = [IO.Path]::GetFullPath($SdImage)

function ConvertTo-WslPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path).Replace("\", "/")
    $converted = & wsl.exe wslpath -a -u $fullPath
    if ($LASTEXITCODE -ne 0) {
        throw "wslpath failed for $Path"
    }
    return ($converted | Select-Object -First 1).Trim()
}

if (-not $SkipSetup) {
    & (Join-Path $project "setup-qemu-sdspi.ps1")
}
foreach ($image in @($flash, $sd)) {
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        throw "Image does not exist: $image"
    }
}
if (-not (Test-Path -LiteralPath $qemu -PathType Leaf)) {
    throw "Patched QEMU does not exist: $qemu"
}

$wslQemu = ConvertTo-WslPath $qemu
$wslFlash = ConvertTo-WslPath $flash
$wslSd = ConvertTo-WslPath $sd
& wsl.exe $wslQemu @(
    "-machine", "esp32,sdspi=on",
    "-nographic",
    "-drive", "file=$wslFlash,if=mtd,format=raw",
    "-drive", "file=$wslSd,if=sd,format=raw"
)
if ($LASTEXITCODE -ne 0) {
    throw "QEMU exited with code $LASTEXITCODE."
}
