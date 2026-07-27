[CmdletBinding()]
param(
    [string]$InputFile,
    [string]$OutputFile = (Join-Path (Split-Path $PSScriptRoot -Parent) "out\HLV\video.hlv"),
    [ValidateRange(1, 30)][int]$Fps = 15,
    [ValidateRange(1, 100)][int]$Quality = 45,
    [ValidateRange(1, 120)][int]$Duration = 10,
    [ValidateRange(16, 320)][int]$Width = 256,
    [ValidateRange(16, 240)][int]$Height = 192,
    [ValidateRange(8000, 48000)][int]$AudioRate = 16000,
    [ValidateRange(-3.0, -0.01)][double]$AudioPeakDb = -0.1,
    [Alias("NoAudioCompression")]
    [switch]$NoAudioNormalization,
    [switch]$NoAudio
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$encoder = Join-Path $repo "build\msvc\hlvenc.exe"
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$videoWidth = $Width
$videoHeight = $Height

if (($videoWidth % 2) -or ($videoHeight % 2)) {
    throw "HLV YUV420 dimensions must be even."
}

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

function Get-PeakSafeAudioFilter {
    param(
        [string]$Ffmpeg,
        [string[]]$InputArguments,
        [int]$Rate,
        [double]$TargetPeakDb
    )

    $conversion = "aformat=channel_layouts=mono,aresample=$Rate"
    $levelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
        "attack=0.01:release=250:knee=8:" +
        "link=maximum:detection=peak"
    $analysisFilter = (
        "$conversion,$levelCurve,astats=metadata=0:reset=0"
    )
    $analysisOutput = & $Ffmpeg -hide_banner -nostats `
        @InputArguments -af $analysisFilter `
        -ac 1 -ar $Rate -f null NUL 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg audio analysis failed with exit code $LASTEXITCODE."
    }

    $peakMatches = [regex]::Matches(
        ($analysisOutput -join "`n"),
        "Peak level dB:\s*(-?\d+(?:\.\d+)?)"
    )
    if (-not $peakMatches.Count) {
        throw "ffmpeg did not report the audio peak after the level curve."
    }

    $culture = [Globalization.CultureInfo]::InvariantCulture
    $curvePeakDb = (
        $peakMatches |
            ForEach-Object {
                [double]::Parse($_.Groups[1].Value, $culture)
            } |
            Measure-Object -Maximum
    ).Maximum
    $outputGainDb = $TargetPeakDb - $curvePeakDb
    $maximumMakeupDb = 20.0 * [Math]::Log10(64.0)
    if ($outputGainDb -lt 0.0 -or $outputGainDb -gt $maximumMakeupDb) {
        $rangeMessage = (
            "Audio normalization requires {0:N3} dB gain, outside " +
            "acompressor makeup's supported 0..{1:N3} dB range."
        ) -f $outputGainDb, $maximumMakeupDb
        throw $rangeMessage
    }

    $makeupText = $outputGainDb.ToString("0.000", $culture)
    [pscustomobject]@{
        Filter = "$conversion,${levelCurve}:makeup=${makeupText}dB"
        CurvePeakDb = $curvePeakDb
        OutputGainDb = $outputGainDb
        TargetPeakDb = $TargetPeakDb
    }
}

try {
    $filter = "fps=$Fps,scale=${videoWidth}:${videoHeight}:" +
        "force_original_aspect_ratio=decrease:flags=lanczos," +
        "pad=${videoWidth}:${videoHeight}:(ow-iw)/2:(oh-ih)/2:black," +
        "format=yuv420p"

    $haveAudio = $false

    if ($InputFile) {
        $InputFile = (Resolve-Path -LiteralPath $InputFile).Path
        & $ffmpeg -y -hide_banner -loglevel error -i $InputFile -an `
            -vf $filter -f yuv4mpegpipe $temporaryY4m
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg video conversion failed." }

        if (-not $NoAudio) {
            & $ffmpeg -v error -i $InputFile -map 0:a:0 `
                -frames:a 1 -f null - 2>$null
            if ($LASTEXITCODE -eq 0) {
                $audioInputArguments = @(
                    "-i", $InputFile, "-map", "0:a:0", "-vn"
                )
                $audioArguments = @(
                    "-y", "-hide_banner", "-loglevel", "error"
                ) + $audioInputArguments
                if (-not $NoAudioNormalization) {
                    $normalization = Get-PeakSafeAudioFilter `
                        -Ffmpeg $ffmpeg `
                        -InputArguments $audioInputArguments `
                        -Rate $AudioRate `
                        -TargetPeakDb $AudioPeakDb
                    $audioArguments += @("-af", $normalization.Filter)
                    $normalizationMessage = (
                        "Audio normalization: curve peak {0:N2} dBFS, " +
                        "output gain {1:N3} dB, target {2:N2} dBFS."
                    ) -f $normalization.CurvePeakDb,
                        $normalization.OutputGainDb,
                        $normalization.TargetPeakDb
                    Write-Host $normalizationMessage
                }
                $audioArguments += @(
                    "-ac", "1", "-ar", $AudioRate,
                    "-f", "u8", $temporaryAudio
                )
                & $ffmpeg @audioArguments
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
            $testAudioInput = @(
                "-f", "lavfi",
                "-i",
                "sine=frequency=440:sample_rate=${AudioRate}:duration=$Duration"
            )
            $audioArguments = @(
                "-y", "-hide_banner", "-loglevel", "error"
            ) + $testAudioInput
            if (-not $NoAudioNormalization) {
                $normalization = Get-PeakSafeAudioFilter `
                    -Ffmpeg $ffmpeg `
                    -InputArguments $testAudioInput `
                    -Rate $AudioRate `
                    -TargetPeakDb $AudioPeakDb
                $audioArguments += @("-af", $normalization.Filter)
                $normalizationMessage = (
                    "Audio normalization: curve peak {0:N2} dBFS, " +
                    "output gain {1:N3} dB, target {2:N2} dBFS."
                ) -f $normalization.CurvePeakDb,
                    $normalization.OutputGainDb,
                    $normalization.TargetPeakDb
                Write-Host $normalizationMessage
            }
            $audioArguments += @(
                "-ac", "1", "-ar", $AudioRate,
                "-f", "u8", $temporaryAudio
            )
            & $ffmpeg @audioArguments
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
    Write-Host "Copy it to the microSD root as video.hlv."
} finally {
    if (Test-Path -LiteralPath $temporaryY4m) {
        Remove-Item -LiteralPath $temporaryY4m -Force
    }
    if (Test-Path -LiteralPath $temporaryAudio) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
