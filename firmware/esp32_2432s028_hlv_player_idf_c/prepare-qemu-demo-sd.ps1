#requires -Version 7.4

[CmdletBinding()]
param(
    [string]$InputFile,
    [string]$OutputAvi,
    [string]$OutputImage,
    [ValidateRange(64, 2048)]
    [int]$ImageSizeMiB = 128
)

$ErrorActionPreference = "Stop"

$project = $PSScriptRoot
$repository = [IO.Path]::GetFullPath((Join-Path $project "..\.."))
if (-not $InputFile) {
    $InputFile = Join-Path $repository (
        "out\sources\big_buck_bunny_1080p_h264\" +
        "big_buck_bunny_1080p_h264.mov"
    )
}
if (-not $OutputAvi) {
    $OutputAvi = Join-Path $repository (
        "out\H263\BigBuckBunny_352x288_24fps_5min_" +
        "H263_CIF_q6.avi"
    )
}
if (-not $OutputImage) {
    $OutputImage = Join-Path $project (
        "qemu\hlv-big-buck-bunny-5min-h263-avi.img"
    )
}

$InputFile = [IO.Path]::GetFullPath($InputFile)
$OutputAvi = [IO.Path]::GetFullPath($OutputAvi)
$OutputImage = [IO.Path]::GetFullPath($OutputImage)
$selection = Join-Path $project "qemu\demo\play.txt"
$testMarker = Join-Path $project "qemu\demo\qemu.txt"
$ffprobe = Join-Path $repository "local_tools\ffmpeg\bin\ffprobe.exe"

if (($ImageSizeMiB -band ($ImageSizeMiB - 1)) -ne 0) {
    throw "QEMU SD-card size must be a power of two in MiB."
}
if (-not (Test-Path -LiteralPath $InputFile -PathType Leaf)) {
    throw "Big Buck Bunny source does not exist: $InputFile"
}
if (-not (Test-Path -LiteralPath $ffprobe -PathType Leaf)) {
    & (Join-Path $repository "scripts\bootstrap_ffmpeg.ps1")
}

$rateText = & $ffprobe -v error -select_streams v:0 `
    -show_entries stream=r_frame_rate -of default=nw=1:nk=1 $InputFile
if ($LASTEXITCODE -ne 0 -or $rateText -notmatch "^(\d+)/(\d+)$") {
    throw "Could not determine the source frame rate."
}
$fps = [double]$Matches[1] / [double]$Matches[2]
$frameCount = [int][Math]::Round(5 * 60 * $fps)
if ([Math]::Abs($frameCount - 5 * 60 * $fps) -gt 0.0001) {
    throw "Five minutes is not an integral source-frame count."
}

& (Join-Path $repository "scripts\encode_h263_avi.ps1") `
    -InputFile $InputFile `
    -OutputFile $OutputAvi `
    -Profile "352x288" `
    -FitMode Crop `
    -VideoQuality 6 `
    -Gop 1 `
    -MaxFrames $frameCount

$imageBytes = [int64]$ImageSizeMiB * 1MB
$avi = Get-Item -LiteralPath $OutputAvi
if ($avi.Length + 16MB -gt $imageBytes) {
    throw (
        "The ${ImageSizeMiB} MiB image is too small for the " +
        "$($avi.Length)-byte AVI."
    )
}
New-Item -ItemType Directory -Force -Path (
    Split-Path $OutputImage -Parent
) | Out-Null

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

    $quote = [string][char]39
    $replacement = $quote + '"' + $quote + '"' + $quote
    return $quote + $Value.Replace($quote, $replacement) + $quote
}

$wslImage = Quote-Bash (ConvertTo-WslPath $OutputImage)
$wslAvi = Quote-Bash (ConvertTo-WslPath $OutputAvi)
$wslSelection = Quote-Bash (ConvertTo-WslPath $selection)
$wslTestMarker = Quote-Bash (ConvertTo-WslPath $testMarker)
$size = $ImageSizeMiB.ToString(
    [Globalization.CultureInfo]::InvariantCulture
)
& wsl.exe bash -lc (
    "truncate -s ${size}M $wslImage && " +
    "mkfs.vfat -F 32 -n HLVDEMO $wslImage >/dev/null && " +
    "mmd -i $wslImage ::/HLV && " +
    "mcopy -i $wslImage $wslAvi ::/HLV/bunny.avi && " +
    "mcopy -i $wslImage $wslSelection ::/HLV/play.txt && " +
    "mcopy -i $wslImage $wslTestMarker ::/HLV/qemu.txt && " +
    "mdir -i $wslImage ::/HLV"
)
if ($LASTEXITCODE -ne 0) {
    throw "Could not create the FAT32 QEMU demo image."
}

$image = Get-Item -LiteralPath $OutputImage
Write-Host (
    "Ready: {0} ({1:N0} bytes), {2} H.263 frames" -f
    $image.FullName,
    $image.Length,
    $frameCount
)
