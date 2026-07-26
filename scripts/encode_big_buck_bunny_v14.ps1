#requires -Version 7.4

[CmdletBinding()]
param(
    [ValidateRange(0, 30)]
    [int]$Fps = 0,

    [ValidateRange(1, 8)]
    [int]$Threads = 4,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$DisableSimd,

    [string]$OutputFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo `
    "out\sources\big_buck_bunny_1080p_h264\big_buck_bunny_1080p_h264.mov"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"

$fpsLabel = if ($Fps) { "${Fps}fps" } else { "native-fps" }
if (-not $OutputFile) {
    $OutputFile = Join-Path $repo `
        ("out\HLV\BigBuckBunny_1080p_video-settings_v14_{0}_level-curve.hlv" -f
            $fpsLabel)
}
if (-not [IO.Path]::IsPathRooted($OutputFile)) {
    $OutputFile = [IO.Path]::GetFullPath($OutputFile)
}

if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p source is missing: $source"
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not (Test-Path -LiteralPath $encoder)) {
    & (Join-Path $PSScriptRoot "build_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $encoder)) {
    throw "FFmpeg or hlvenc is unavailable."
}

$outputParent = Split-Path $OutputFile -Parent
if ($outputParent) {
    New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
}

$temporaryAudio = Join-Path ([IO.Path]::GetTempPath()) `
    ("hlv-bbb-v14-{0}.u8" -f [guid]::NewGuid().ToString("N"))
$audioConversion = "aformat=channel_layouts=mono,aresample=16000"
$audioLevelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
    "attack=0.01:release=250:knee=8:" +
    "link=maximum:detection=peak"
$audioPeakTargetDb = -0.1
$videoFilters = @(
    "scale=320:180:force_original_aspect_ratio=decrease:flags=lanczos",
    "pad=320:180:(ow-iw)/2:(oh-ih)/2:black",
    "format=yuv420p"
)
if ($Fps) {
    $videoFilters = @("fps=$Fps") + $videoFilters
}
$videoFilter = $videoFilters -join ","

try {
    Write-Host (
        "Measuring the level curve on the 1080p MOV source " +
        "(no EQ, loudness filter, separate volume or limiter)..."
    )
    $analysisFilter = (
        "$audioConversion,$audioLevelCurve," +
        "astats=metadata=0:reset=0"
    )
    $analysisOutput = & $ffmpeg -hide_banner -nostats -i $source `
        -map 0:a:0 -vn -af $analysisFilter `
        -ac 1 -ar 16000 -f null NUL 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg audio analysis failed with exit code $LASTEXITCODE."
    }
    $peakMatches = [regex]::Matches(
        ($analysisOutput -join "`n"),
        "Peak level dB:\s*(-?\d+(?:\.\d+)?)"
    )
    if (-not $peakMatches.Count) {
        throw "FFmpeg did not report the audio peak after the level curve."
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $curvePeakDb = (
        $peakMatches |
            ForEach-Object {
                [double]::Parse($_.Groups[1].Value, $culture)
            } |
            Measure-Object -Maximum
    ).Maximum
    $curveMakeupDb = $audioPeakTargetDb - $curvePeakDb
    if ($curveMakeupDb -lt 0.0) {
        $attenuationMessage = (
            "The level curve needs {0:N3} dB attenuation, but acompressor " +
            "makeup cannot attenuate."
        ) -f $curveMakeupDb
        throw $attenuationMessage
    }
    $curveMakeupText = $curveMakeupDb.ToString("0.000", $culture)
    $audioFilter = (
        "$audioConversion,${audioLevelCurve}:makeup=${curveMakeupText}dB"
    )
    $audioStatusMessage = (
        "Preparing PCM: unscaled peak {0:N2} dBFS, curve makeup {1} dB, " +
        "target {2:N1} dBFS..."
    ) -f $curvePeakDb, $curveMakeupText, $audioPeakTargetDb
    Write-Host $audioStatusMessage
    & $ffmpeg -y -hide_banner -loglevel error -i $source `
        -map 0:a:0 -vn -af $audioFilter `
        -ac 1 -ar 16000 -f u8 $temporaryAudio
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg audio conversion failed with exit code $LASTEXITCODE."
    }

    $ffmpegArguments = @(
        "-y", "-hide_banner", "-loglevel", "error",
        "-i", $source, "-an", "-vf", $videoFilter
    )
    if ($MaxFrames) {
        $ffmpegArguments += @("-frames:v", $MaxFrames)
    }
    $ffmpegArguments += @("-f", "yuv4mpegpipe", "-")

    $encoderArguments = @(
        "-", $OutputFile,
        "--preset", "balanced",
        "--quality", 45,
        "--gop", 30,
        "--syntax", 14,
        "--threads", $Threads,
        "--audio-u8", $temporaryAudio,
        "--audio-rate", 16000
    )
    if ($MaxFrames) {
        $encoderArguments += @("--max-frames", $MaxFrames)
    }
    if ($DisableSimd) {
        $encoderArguments += @("--simd", "off")
    }

    $frameRateMessage = if ($Fps) {
        "$Fps fps"
    } else {
        "the native source frame rate"
    }
    $profileMessage = (
        "Encoding 320x180 at {0}, stable HLV v14, quality 45, " +
        "GOP 30, threads {1}..."
    ) -f $frameRateMessage, $Threads
    Write-Host $profileMessage
    & $ffmpeg @ffmpegArguments | & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "HLV encoding failed with exit code $LASTEXITCODE."
    }

    $result = Get-Item -LiteralPath $OutputFile
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f
        $result.FullName, $result.Length)
} finally {
    if (Test-Path -LiteralPath $temporaryAudio) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
