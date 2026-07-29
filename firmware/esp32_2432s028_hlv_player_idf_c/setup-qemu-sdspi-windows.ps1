[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$qemuRoot = Join-Path $repository "local_tools\qemu-sdspi-windows"
$source = Join-Path $qemuRoot "source"
$runtime = $qemuRoot
$msysParent = Join-Path $repository "local_tools\msys2"
$msysRoot = Join-Path $msysParent "msys64"
$bash = Join-Path $msysRoot "usr\bin\bash.exe"
$msysVersion = "20260611"
$msysArchive = Join-Path $msysParent (
    "msys2-base-x86_64-$msysVersion.sfx.exe"
)
$msysUri = (
    "https://repo.msys2.org/distrib/x86_64/" +
    "msys2-base-x86_64-$msysVersion.sfx.exe"
)
$msysSha256 = (
    "C105946E64E08F099AC0E4647461CE762B95333AD211777666476A9A41451D65"
)
$qemuCommit = "40edccac415693c5130f91c01d84176ae6008566"
$qemuTag = "esp-develop-9.2.2-20260417"
$patches = @(
    (Join-Path $project "qemu\patches\0001-esp32-sdspi.patch"),
    (Join-Path $project "qemu\patches\0003-esp32-gpio-input.patch"),
    (Join-Path $project "qemu\patches\0004-ssi-sd-bulk-read.patch"),
    (Join-Path $project "qemu\patches\0005-realtime-sd-display-audio.patch"),
    (Join-Path $project "qemu\patches\0002-windows-symlink-fallback.patch")
)
$packages = @(
    "base-devel",
    "git",
    "diffutils",
    "mingw-w64-x86_64-gcc",
    "mingw-w64-x86_64-python",
    "mingw-w64-x86_64-ninja",
    "mingw-w64-x86_64-pkgconf",
    "mingw-w64-x86_64-glib2",
    "mingw-w64-x86_64-pixman",
    "mingw-w64-x86_64-libgcrypt",
    "mingw-w64-x86_64-SDL2",
    "mingw-w64-x86_64-zlib",
    "mingw-w64-x86_64-libslirp"
)

function Quote-Bash {
    param([Parameter(Mandatory)][string]$Value)

    $singleQuote = [string][char]39
    $replacement = $singleQuote + '"' + $singleQuote + '"' + $singleQuote
    return $singleQuote + $Value.Replace($singleQuote, $replacement) +
        $singleQuote
}

function ConvertTo-MsysPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $converted = & $bash -lc (
        "cygpath -a -u " + (Quote-Bash $fullPath)
    )
    if ($LASTEXITCODE -ne 0) {
        throw "cygpath failed for $fullPath"
    }
    return ($converted | Select-Object -First 1).Trim()
}

function Invoke-Msys {
    param([Parameter(Mandatory)][string]$Command)

    & $bash -lc ("export PATH=/mingw64/bin:/usr/bin; " + $Command)
    if ($LASTEXITCODE -ne 0) {
        throw "MSYS2 command failed: $Command"
    }
}

$newMsysInstall = $false
New-Item -ItemType Directory -Force -Path $msysParent | Out-Null
if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) {
    if (-not (Test-Path -LiteralPath $msysArchive -PathType Leaf)) {
        Write-Host "Downloading MSYS2 $msysVersion"
        Invoke-WebRequest -Uri $msysUri -OutFile $msysArchive
    }

    $actualSha256 = (Get-FileHash -Algorithm SHA256 $msysArchive).Hash
    if ($actualSha256 -ne $msysSha256) {
        throw (
            "MSYS2 archive SHA-256 mismatch. Expected $msysSha256, " +
            "found $actualSha256."
        )
    }

    Write-Host "Extracting MSYS2 under local_tools"
    & $msysArchive -y ("-o" + $msysParent)
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $bash -PathType Leaf)) {
        throw "Could not extract MSYS2."
    }
    $newMsysInstall = $true
}

$env:CHERE_INVOKING = "yes"
$env:MSYSTEM = "MINGW64"

if ($newMsysInstall) {
    Write-Host "Initializing the signed MSYS2 package keyring"
    Invoke-Msys "pacman-key --init && pacman-key --populate msys2"
    Invoke-Msys "pacman-key --refresh-keys"
    # This exact official development-key fingerprint is pinned because a
    # fresh 20260611 archive does not yet trust its July 2026 signatures.
    Invoke-Msys (
        "pacman-key --lsign-key " +
        "5F944B027F7FE2091985AA2EFA11531AA0AA7F57 && " +
        "pacman-key --updatedb"
    )
}

Write-Host "Installing the MinGW64 QEMU build dependencies"
Invoke-Msys (
    "pacman -Sy --noconfirm --needed " + ($packages -join " ")
)

foreach ($patch in $patches) {
    if (-not (Test-Path -LiteralPath $patch -PathType Leaf)) {
        throw "Missing QEMU patch: $patch"
    }
}

New-Item -ItemType Directory -Force -Path $qemuRoot | Out-Null
$msysSource = ConvertTo-MsysPath $source
$quotedSource = Quote-Bash $msysSource

if (-not (Test-Path -LiteralPath (Join-Path $source ".git"))) {
    if (Test-Path -LiteralPath $source) {
        throw "The existing QEMU source directory is not a Git checkout: $source"
    }
    Write-Host "Cloning Espressif QEMU $qemuTag"
    Invoke-Msys (
        "git -c core.symlinks=false clone --depth 1 --branch $qemuTag " +
        "https://github.com/espressif/qemu.git $quotedSource"
    )
}

$actualCommit = (& $bash -lc (
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
        & git -C $source apply --reverse --check $patch 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -C $source apply --check $patch
            if ($LASTEXITCODE -ne 0) {
                throw "Could not validate QEMU patch: $patch"
            }
            & git -C $source apply $patch
            if ($LASTEXITCODE -ne 0) {
                throw "Could not apply QEMU patch: $patch"
            }
        }
    }
    Set-Content -LiteralPath $patchMarker `
        -Value $expectedPatchMarker -NoNewline
}

$configMarker = Join-Path $source "build\.hlv-windows-config"
$expectedMarker = "$qemuCommit`n$patchDigest"
$needsConfigure = -not (
    (Test-Path -LiteralPath (Join-Path $source "build\build.ninja")) -and
    (Test-Path -LiteralPath $configMarker) -and
    ((Get-Content -Raw -LiteralPath $configMarker) -eq $expectedMarker)
)

if ($needsConfigure) {
    Write-Host "Configuring native Windows xtensa-softmmu with SDL"
    Invoke-Msys (
        "cd $quotedSource && ./configure " +
        "--bindir=bin --datadir=share/qemu " +
        "--enable-gcrypt --enable-pixman --enable-sdl --enable-slirp " +
        "--enable-stack-protector --disable-werror " +
        "--prefix=`"`$PWD/install/qemu`" --static " +
        "--target-list=xtensa-softmmu " +
        "--with-pkgversion=`"HLV ST7789 SDSPI Windows`" " +
        "--with-suffix= --without-default-features"
    )
    Set-Content -LiteralPath $configMarker -Value $expectedMarker -NoNewline
}

Write-Host "Building native qemu-system-xtensa.exe"
Invoke-Msys "cd $quotedSource && ninja -C build qemu-system-xtensa.exe"

$runtimeBin = Join-Path $runtime "bin"
$runtimeData = Join-Path $runtime "share\qemu"
New-Item -ItemType Directory -Force -Path $runtimeBin | Out-Null
New-Item -ItemType Directory -Force -Path $runtimeData | Out-Null
Copy-Item -LiteralPath (
    Join-Path $source "build\qemu-system-xtensa.exe"
) -Destination $runtimeBin -Force
foreach ($rom in @("esp32-v3-rom.bin", "esp32-v3-rom-app.bin")) {
    Copy-Item -LiteralPath (
        Join-Path $source "pc-bios\$rom"
    ) -Destination $runtimeData -Force
}
foreach ($legalFile in @("LICENSE", "COPYING", "COPYING.LIB")) {
    Copy-Item -LiteralPath (
        Join-Path $source $legalFile
    ) -Destination $runtime -Force
}

$qemu = Join-Path $runtimeBin "qemu-system-xtensa.exe"
& $qemu --version
if ($LASTEXITCODE -ne 0) {
    throw "The native Windows QEMU smoke check failed."
}

Write-Host "Native Windows ESP32 QEMU is ready: $qemu"
