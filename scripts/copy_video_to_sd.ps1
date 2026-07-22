[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$DestinationRoot,
    [string]$InputFile = `
        (Join-Path (Split-Path $PSScriptRoot -Parent) "out\video.hlv")
)

$ErrorActionPreference = "Stop"
$InputFile = (Resolve-Path -LiteralPath $InputFile).Path
$destination = (Resolve-Path -LiteralPath $DestinationRoot).Path
$destinationItem = Get-Item -LiteralPath $destination
if (-not $destinationItem.PSIsContainer) {
    throw "DestinationRoot must be the mounted SD-card root directory."
}

$sourceInfo = Get-Item -LiteralPath $InputFile
$drive = [IO.DriveInfo]::new($destinationItem.PSDrive.Root)
if (-not $drive.IsReady) {
    throw "The destination drive is not ready."
}
if ($drive.AvailableFreeSpace -lt $sourceInfo.Length) {
    throw "The SD card does not have enough free space."
}

$output = Join-Path $destination "video.hlv"
Copy-Item -LiteralPath $InputFile -Destination $output -Force

$sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $InputFile).Hash
$outputHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $output).Hash
if ($sourceHash -ne $outputHash) {
    throw "SHA-256 verification failed after copying video.hlv."
}

$outputInfo = Get-Item -LiteralPath $output
Write-Host ("Ready: {0} ({1:N0} bytes)" -f $outputInfo.FullName,
    $outputInfo.Length)
Write-Host "SHA-256: $outputHash"
