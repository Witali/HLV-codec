[CmdletBinding()]
param(
    [string]$InputFile,
    [string]$OutputFile = (Join-Path (Split-Path $PSScriptRoot -Parent) "out\video.hlv"),
    [ValidateRange(1, 30)][int]$Fps = 15,
    [ValidateRange(1, 100)][int]$Quality = 45,
    [ValidateRange(1, 120)][int]$Duration = 10,
    [ValidateRange(8000, 48000)][int]$AudioRate = 16000,
    [ValidateRange(0.0, 1.0)][double]$AudioVolume = 0.20,
    [switch]$NoAudio
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"
$ffmpeg = Join-Path $repo "tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "tools\ffmpeg\bin\ffprobe.exe"
$videoWidth = 256
$videoHeight = 192

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
$temporaryAudio = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv1-esp32-{0}.u8" -f [guid]::NewGuid().ToString("N"))

try {
    $filter = "fps=$Fps,scale=${videoWidth}:${videoHeight}:" +
        "force_original_aspect_ratio=decrease:flags=lanczos," +
        "pad=${videoWidth}:${videoHeight}:(ow-iw)/2:(oh-ih)/2:black," +
        "format=yuv420p"

    $haveAudio = $false
    $audioVolumeText = $AudioVolume.ToString(
        [Globalization.CultureInfo]::InvariantCulture)

    if ($InputFile) {
        $InputFile = (Resolve-Path -LiteralPath $InputFile).Path
        & $ffmpeg -y -hide_banner -loglevel error -i $InputFile -an `
            -vf $filter -f yuv4mpegpipe $temporaryY4m
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg video conversion failed." }

        if (-not $NoAudio) {
            $audioStreams = & $ffprobe -v error -select_streams a:0 `
                -show_entries stream=index -of "csv=p=0" $InputFile
            if ($LASTEXITCODE -ne 0) { throw "ffprobe audio detection failed." }
            if ($audioStreams) {
                & $ffmpeg -y -hide_banner -loglevel error -i $InputFile `
                    -map 0:a:0 -vn -af "volume=$audioVolumeText" `
                    -ac 1 -ar $AudioRate -f u8 $temporaryAudio
                if ($LASTEXITCODE -ne 0) { throw "ffmpeg audio conversion failed." }
                $haveAudio = $true
            } else {
                Write-Warning "Input has no audio stream; creating video-only HLV."
            }
        }
    } else {
        & $ffmpeg -y -hide_banner -loglevel error -f lavfi `
            -i "testsrc2=size=${videoWidth}x${videoHeight}:rate=${Fps}:duration=$Duration" -an `
            -vf "format=yuv420p" -f yuv4mpegpipe $temporaryY4m
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg test video generation failed." }
        if (-not $NoAudio) {
            & $ffmpeg -y -hide_banner -loglevel error -f lavfi `
                -i "sine=frequency=440:sample_rate=${AudioRate}:duration=$Duration" `
                -af "volume=$audioVolumeText" -ac 1 -ar $AudioRate `
                -f u8 $temporaryAudio
            if ($LASTEXITCODE -ne 0) { throw "ffmpeg test audio generation failed." }
            $haveAudio = $true
        }
    }
    $encoderArguments = @(
        $temporaryY4m, $OutputFile,
        "--preset", "balanced",
        "--quality", $Quality,
        "--gop", 30,
        "--syntax", 12
    )
    if ($haveAudio) {
        $encoderArguments += @(
            "--audio-u8", $temporaryAudio,
            "--audio-rate", $AudioRate
        )
    }
    & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) { throw "HLV-1 encoding failed." }

    $size = (Get-Item -LiteralPath $OutputFile).Length
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f $OutputFile, $size)
    Write-Host "build_esp32.ps1 will package it into the internal LittleFS image."
} finally {
    if (Test-Path -LiteralPath $temporaryY4m) {
        Remove-Item -LiteralPath $temporaryY4m -Force
    }
    if (Test-Path -LiteralPath $temporaryAudio) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
