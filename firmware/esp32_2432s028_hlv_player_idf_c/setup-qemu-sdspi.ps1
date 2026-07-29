[CmdletBinding()]
param(
    [switch]$InstallWslDependencies
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$qemuRoot = Join-Path $repository "local_tools\qemu-sdspi"
$source = Join-Path $qemuRoot "source"
$build = Join-Path $qemuRoot "build"
$configMarker = Join-Path $build ".hlv-sdspi-st7789-sdl-v1"
$patches = @(
    (Join-Path $project "qemu\patches\0001-esp32-sdspi.patch"),
    (Join-Path $project "qemu\patches\0003-esp32-gpio-input.patch"),
    (Join-Path $project "qemu\patches\0004-ssi-sd-bulk-read.patch"),
    (Join-Path $project "qemu\patches\0005-realtime-sd-display-audio.patch")
)
$qemuCommit = "40edccac415693c5130f91c01d84176ae6008566"
$qemuTag = "esp-develop-9.2.2-20260417"
$packages = @(
    "build-essential",
    "git",
    "libgcrypt20-dev",
    "libglib2.0-dev",
    "libpixman-1-dev",
    "libsdl2-dev",
    "libslirp-dev",
    "mtools",
    "ninja-build",
    "pkg-config",
    "python3-venv",
    "dosfstools"
)

function ConvertTo-WslPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $converted = & wsl.exe wslpath -a -u $fullPath.Replace("\", "/")
    if ($LASTEXITCODE -ne 0) {
        throw "wslpath failed for $fullPath"
    }
    return ($converted | Select-Object -First 1).Trim()
}

function Quote-Bash {
    param([Parameter(Mandatory)][string]$Value)

    $singleQuote = [string][char]39
    $replacement = $singleQuote + '"' + $singleQuote + '"' + $singleQuote
    return $singleQuote + $Value.Replace($singleQuote, $replacement) +
        $singleQuote
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "WSL is required to build the patched Espressif QEMU."
}
foreach ($patch in $patches) {
    if (-not (Test-Path -LiteralPath $patch)) {
        throw "Missing QEMU patch: $patch"
    }
}

$packageList = $packages -join " "
& wsl.exe bash -lc "dpkg-query -W $packageList >/dev/null 2>&1"
if ($LASTEXITCODE -ne 0) {
    if (-not $InstallWslDependencies) {
        throw ("Required WSL packages are missing. Re-run with " +
            "-InstallWslDependencies.")
    }
    & wsl.exe sudo apt-get update
    if ($LASTEXITCODE -ne 0) {
        throw "apt-get update failed."
    }
    & wsl.exe sudo apt-get install -y @packages
    if ($LASTEXITCODE -ne 0) {
        throw "Could not install the QEMU build dependencies."
    }
}

New-Item -ItemType Directory -Force -Path $qemuRoot | Out-Null
$wslSource = ConvertTo-WslPath $source
$wslBuild = ConvertTo-WslPath $build
$quotedSource = Quote-Bash $wslSource
$quotedBuild = Quote-Bash $wslBuild

if (-not (Test-Path -LiteralPath (Join-Path $source ".git"))) {
    if (Test-Path -LiteralPath $source) {
        throw "The existing QEMU source directory is not a Git checkout: $source"
    }
    Write-Host "Cloning Espressif QEMU $qemuTag"
    & wsl.exe bash -lc (
        "git clone --depth 1 --branch $qemuTag " +
        "https://github.com/espressif/qemu.git $quotedSource"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Could not clone Espressif QEMU."
    }
}

$actualCommit = (& wsl.exe bash -lc (
    "git -C $quotedSource rev-parse HEAD"
)).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $qemuCommit) {
    throw "Expected QEMU commit $qemuCommit, found $actualCommit."
}

$patchDigest = (
    Get-FileHash -Algorithm SHA256 $patches |
        ForEach-Object Hash
) -join ":"
$patchMarker = Join-Path $source ".hlv-patch-set"
$expectedPatchMarker = "$qemuCommit`n$patchDigest"
if (-not (
    (Test-Path -LiteralPath $patchMarker) -and
    ((Get-Content -Raw -LiteralPath $patchMarker) -eq $expectedPatchMarker)
)) {
    foreach ($patch in $patches) {
        $quotedPatch = Quote-Bash (ConvertTo-WslPath $patch)
        & wsl.exe bash -lc (
            "git -C $quotedSource apply --reverse --check $quotedPatch " +
            ">/dev/null 2>&1"
        )
        if ($LASTEXITCODE -ne 0) {
            & wsl.exe bash -lc (
                "git -C $quotedSource apply --check $quotedPatch && " +
                "git -C $quotedSource apply $quotedPatch"
            )
            if ($LASTEXITCODE -ne 0) {
                throw "Could not apply QEMU patch: $patch"
            }
        }
    }
    Set-Content -LiteralPath $patchMarker `
        -Value $expectedPatchMarker -NoNewline
}

$expectedMarker = "$qemuCommit`n$patchDigest"
if (-not (Test-Path -LiteralPath (Join-Path $build "build.ninja")) -or
    -not (Test-Path -LiteralPath $configMarker) -or
    (Get-Content -Raw -LiteralPath $configMarker) -ne $expectedMarker) {
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    Write-Host "Configuring the patched Xtensa QEMU with SDL"
    & wsl.exe bash -lc (
        "cd $quotedBuild && $quotedSource/configure " +
        "--target-list=xtensa-softmmu --without-default-features " +
        "--enable-gcrypt --enable-pixman --enable-sdl --enable-slirp " +
        "--enable-stack-protector --with-pkgversion=HLV-SDSPI"
    )
    if ($LASTEXITCODE -ne 0) {
        throw "QEMU configuration failed."
    }
    Set-Content -LiteralPath $configMarker -Value $expectedMarker -NoNewline
}

Write-Host "Building the patched Xtensa QEMU"
& wsl.exe bash -lc "ninja -C $quotedBuild qemu-system-xtensa"
if ($LASTEXITCODE -ne 0) {
    throw "QEMU build failed."
}

Write-Host ("ESP32 SDSPI/ST7789 QEMU is ready: " +
    (Join-Path $build "qemu-system-xtensa"))
