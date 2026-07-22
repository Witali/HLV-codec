[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Port,
    [switch]$SkipBuild,
    [ValidateSet(115200, 230400, 460800, 921600)][int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$buildDirectory = Join-Path $repo "build\esp32"
$bootloader = Join-Path $buildDirectory `
    "esp32_2432s028_hlv_player.ino.bootloader.bin"
$partitions = Join-Path $buildDirectory `
    "esp32_2432s028_hlv_player.ino.partitions.bin"
$application = Join-Path $buildDirectory `
    "esp32_2432s028_hlv_player.ino.bin"
$bootApplication = Join-Path $repo `
    "local_tools\arduino\data\packages\esp32\hardware\esp32\3.3.8\tools\partitions\boot_app0.bin"
$esptoolRoot = Join-Path $repo `
    "local_tools\arduino\data\packages\esp32\tools\esptool_py"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot "build_esp32.ps1")
}
$images = @($bootloader, $partitions, $bootApplication, $application)
foreach ($image in $images) {
    if (-not (Test-Path -LiteralPath $image)) {
        throw "Flash image is missing: $image. Run .\scripts\build_esp32.ps1 first."
    }
}

$esptool = Get-ChildItem -LiteralPath $esptoolRoot -Recurse `
    -Filter esptool.exe | Select-Object -First 1 -ExpandProperty FullName
if (-not $esptool) {
    throw "Project-local esptool.exe is missing. Run .\scripts\bootstrap_arduino.ps1 first."
}

Write-Host "Put the board in download mode: hold BOOT, tap RST, then release BOOT."
Write-Host "Waiting for the ROM loader on $Port..."
$writeImages = @(
    "0x1000", $bootloader,
    "0x8000", $partitions,
    "0xe000", $bootApplication,
    "0x10000", $application
)
& $esptool --chip esp32 --port $Port --baud $Baud `
    --before no-reset --after hard-reset --connect-attempts 60 `
    write-flash --flash-mode dio --flash-freq 40m --flash-size 4MB `
    @writeImages
if ($LASTEXITCODE -ne 0) {
    throw "Uploading the SD-player firmware failed."
}

Write-Host "SD-player firmware was uploaded. video.hlv remains on microSD."
