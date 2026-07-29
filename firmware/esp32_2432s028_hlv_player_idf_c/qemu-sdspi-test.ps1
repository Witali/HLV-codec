[CmdletBinding()]
param(
    [switch]$InstallWslDependencies,
    [string]$IdfScript = ""
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
$build = Join-Path $project "build-qemu-sdspi"
$flash = Join-Path $build "qemu_flash_4mb.bin"
$sd = Join-Path $build "qemu_sdcard.img"
$sdSource = Join-Path $project "qemu_sdcard\HLV\qemu.txt"
$qemu = Join-Path $repository "local_tools\qemu-sdspi\build\qemu-system-xtensa"
$idf = if ($IdfScript) {
    [IO.Path]::GetFullPath($IdfScript)
} else {
    Join-Path $project "idf.ps1"
}

function ConvertTo-WslPath {
    param([Parameter(Mandatory)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path).Replace("\", "/")
    $converted = & wsl.exe wslpath -a -u $fullPath
    if ($LASTEXITCODE -ne 0) {
        throw "wslpath failed for $Path"
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

& (Join-Path $project "setup-qemu-sdspi.ps1") `
    -InstallWslDependencies:$InstallWslDependencies

$idfArguments = @(
    "-C", $project,
    "-B", $build,
    "-D", "SDSPI_QEMU_TEST=ON",
    "build"
)
& $idf -IdfArguments $idfArguments

$esptoolArguments = @(
    "--chip", "esp32",
    "merge_bin",
    "-o", $flash,
    "--flash_mode", "dio",
    "--flash_freq", "80m",
    "--flash_size", "4MB",
    "--fill-flash-size", "4MB",
    "0x1000", "bootloader\bootloader.bin",
    "0x8000", "partition_table\partition-table.bin",
    "0x10000", "hlv_esp32_player.bin"
)
& $idf `
    -EsptoolArguments $esptoolArguments `
    -EsptoolWorkingDirectory $build

if (Test-Path -LiteralPath $sd) {
    Remove-Item -LiteralPath $sd -Force
}
$wslSd = Quote-Bash (ConvertTo-WslPath $sd)
$wslSource = Quote-Bash (ConvertTo-WslPath $sdSource)
& wsl.exe bash -lc (
    "truncate -s 64M $wslSd && " +
    "mkfs.vfat -F 32 -n HLVSD $wslSd >/dev/null && " +
    "mmd -i $wslSd ::/HLV && " +
    "mcopy -i $wslSd $wslSource ::/HLV/qemu.txt"
)
if ($LASTEXITCODE -ne 0) {
    throw "Could not create the FAT32 SD-card image."
}

$quotedQemu = Quote-Bash (ConvertTo-WslPath $qemu)
$quotedFlash = Quote-Bash (ConvertTo-WslPath $flash)
$command =
    "timeout --foreground 10s $quotedQemu " +
    "-machine esp32,sdspi=on " +
    "-nographic " +
    "-no-reboot " +
    "-drive file=$quotedFlash,if=mtd,format=raw " +
    "-drive file=$wslSd,if=sd,format=raw 2>&1"
$output = @(& wsl.exe bash -lc $command)
$output | ForEach-Object { Write-Host $_ }
if (-not ($output -contains "SDSPI_QEMU_STAGE,mounted")) {
    throw "The QEMU firmware did not mount the SPI SD card."
}
if (-not ($output -match
    "^SDSPI_QEMU_MULTIBLOCK_READ,16,65536,\d+,4e1b1d51$")) {
    throw "The QEMU firmware did not complete the CMD18 read check."
}
if (-not ($output -contains
    "SDSPI_QEMU_MULTIBLOCK_WRITE,4096,edb6ddc5")) {
    throw "The QEMU firmware did not complete the CMD25 write check."
}
if (-not ($output -contains "SDSPI_QEMU_DONE,0")) {
    throw "The QEMU firmware did not read the expected SD-card file."
}

Write-Host "ESP32 SPI3 SD-card smoke test passed."
