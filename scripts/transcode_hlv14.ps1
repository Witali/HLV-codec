#requires -Version 7.4

<#
.SYNOPSIS
Transcodes one video to stable HLV v14 with adaptive 35..42 dB quality.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [string]$InputFile,

    [string]$OutputFile,

    [ValidateRange(16, 320)]
    [int]$Width = 320,

    [ValidateRange(16, 240)]
    [int]$Height = 240,

    [ValidateSet("Auto", "Stretch", "Crop")]
    [string]$ResizeMode = "Auto",

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$NoAudio,
    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")
. (Join-Path $PSScriptRoot "_audio_normalization.ps1")

$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"
$decoder = Join-Path $repo "build\msvc\hlvdec.exe"
$info = Get-TranscodeVideoInfo -InputFile $InputFile
if (-not (Test-Path -LiteralPath $encoder) -or
    -not (Test-Path -LiteralPath $decoder)) {
    & (Join-Path $PSScriptRoot "build_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $encoder) -or
    -not (Test-Path -LiteralPath $decoder)) {
    throw "HLV encoder/decoder binaries are unavailable."
}

$fpsText = Format-TranscodeNumber -Value $info.Fps
$effectiveResizeMode = if ($ResizeMode -eq "Auto") {
    if ($Height -eq 240) { "Crop" } else { "Stretch" }
}
else {
    $ResizeMode
}
$OutputFile = Get-TranscodeOutputFile `
    -OutputFile $OutputFile `
    -CodecDirectory "HLV" `
    -FileName (
        "$($info.BaseName)_${Width}x${Height}_${fpsText}fps_" +
        "HLVv14_adaptive35-42dB.hlv"
    )
Assert-TranscodeOutput -OutputFile $OutputFile -Force:$Force

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) (
    "hlv-v14-profile-" + [guid]::NewGuid().ToString("N")
)
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
$temporaryVideo = Join-Path $temporaryDirectory "video.y4m"
$temporaryAudio = Join-Path $temporaryDirectory "audio.u8"
$cqLog = [IO.Path]::ChangeExtension($OutputFile, ".cq.csv")

try {
    $videoFilter = Get-Esp32PreparationFilter `
        -Width $Width `
        -Height $Height `
        -ResizeMode $effectiveResizeMode
    $videoArguments = @(
        "-y", "-hide_banner", "-loglevel", "warning", "-stats",
        "-i", $info.InputFile,
        "-map", "0:v:0",
        "-an",
        "-vf", $videoFilter
    )
    if ($MaxFrames) {
        $videoArguments += @("-frames:v", $MaxFrames)
    }
    $videoArguments += @("-f", "yuv4mpegpipe", $temporaryVideo)
    & $ffmpeg @videoArguments
    if ($LASTEXITCODE -ne 0) {
        throw "HLV source video preparation failed."
    }

    $haveAudio = $false
    $audioNormalization = $null
    if (-not $NoAudio) {
        $audioIndex = & $ffprobe -v error -select_streams a:0 `
            -show_entries stream=index -of csv=p=0 $info.InputFile |
            Select-Object -First 1
        $haveAudio = $LASTEXITCODE -eq 0 -and
            -not [string]::IsNullOrWhiteSpace([string]$audioIndex)
        if ($haveAudio) {
            $audioNormalization = Get-PeakSafeAudioFilter `
                -Ffmpeg $ffmpeg `
                -InputFile $info.InputFile `
                -Rate 16000
            $audioArguments = @(
                "-y", "-hide_banner", "-loglevel", "error",
                "-i", $info.InputFile,
                "-map", "0:a:0",
                "-vn",
                "-af", $audioNormalization.Filter,
                "-ac", "1",
                "-ar", "16000"
            )
            if ($MaxFrames) {
                $duration = $MaxFrames / $info.Fps
                $durationText = $duration.ToString(
                    "0.########",
                    [Globalization.CultureInfo]::InvariantCulture
                )
                $audioArguments += @("-t", $durationText)
            }
            $audioArguments += @("-f", "u8", $temporaryAudio)
            & $ffmpeg @audioArguments
            if ($LASTEXITCODE -ne 0) {
                throw "HLV source audio preparation failed."
            }
            Write-AudioNormalizationStatus $audioNormalization
        }
    }

    $encoderArguments = @(
        $temporaryVideo, $OutputFile,
        "--preset", "slow",
        "--syntax", "14",
        "--gop", "45",
        "--adaptive-quality",
        "--psnr-min", "35",
        "--psnr-max", "42",
        "--cq-trials", "5",
        "--cq-log", $cqLog,
        "--threads", "1"
    )
    if ($MaxFrames) {
        $encoderArguments += @("--max-frames", $MaxFrames)
    }
    if ($haveAudio) {
        $encoderArguments += @(
            "--audio-u8", $temporaryAudio,
            "--audio-rate", "16000"
        )
    }
    & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "HLV v14 encoding failed."
    }

    & $decoder $OutputFile NUL
    if ($LASTEXITCODE -ne 0) {
        throw "Full HLV v14 validation failed."
    }
    $cqLines = @(Get-Content -LiteralPath $cqLog).Count
    $encodedFrames = [Math]::Max(0, $cqLines - 1)
    $reportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
    [ordered]@{
        input = $info.InputFile
        output = $OutputFile
        codec = "HLV"
        syntax = 14
        width = $Width
        height = $Height
        fps = $info.Fps
        frames = $encodedFrames
        preset = "slow"
        gop = 45
        qualityMode = "adaptivePsnr"
        psnrMinDb = 35.0
        psnrMaxDb = 42.0
        cqTrials = 5
        threads = 1
        audio = if ($haveAudio) { "PCM_U8 mono 16000 Hz" } else { $null }
        audioNormalization = if ($haveAudio) {
            [ordered]@{
                curve = "primary-compressor-peak"
                targetPeakDb = $audioNormalization.TargetPeakDb
                curvePeakDb = $audioNormalization.CurvePeakDb
                makeupDb = $audioNormalization.OutputGainDb
            }
        } else {
            $null
        }
        cqLog = $cqLog
    } | ConvertTo-Json |
        Set-Content -LiteralPath $reportFile -Encoding utf8
    Write-Host "Ready: $OutputFile"
    Write-Host "CQ decisions: $cqLog"
    Write-Host "Report: $reportFile"
}
finally {
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryDirectory)
    $systemTemporary = [IO.Path]::GetFullPath(
        [IO.Path]::GetTempPath()
    )
    if ($resolvedTemporary.StartsWith(
        $systemTemporary,
        [StringComparison]::OrdinalIgnoreCase
    ) -and (Test-Path -LiteralPath $resolvedTemporary)) {
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}
