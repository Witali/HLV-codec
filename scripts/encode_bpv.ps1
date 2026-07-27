#requires -Version 7.4

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$InputFile,

    [Parameter(Mandatory)]
    [string]$OutputFile,

    [ValidateRange(2, 65534)]
    [int]$Width = 320,

    [ValidateRange(2, 65534)]
    [int]$Height = 240,

    [ValidateSet("Stretch", "Crop")]
    [string]$ResizeMode = "Stretch",

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateSet("Cpu", "Auto", "Cuda")]
    [string]$Device = "Auto",

    [ValidateRange(1, 65535)]
    [int]$Gop = 48,

    [ValidateRange(1, 65535)]
    [int]$MinGop = 12,

    [ValidateRange(0.0, 1.0)]
    [double]$SceneThreshold = 0.35,

    [ValidateRange(0, 1000000000)]
    [double]$Lambda = 64,

    [ValidateRange(1, 64)]
    [int]$CandidatePalettes = 8,

    [ValidateRange(64, 262144)]
    [int]$SampleBlocks = 32768,

    [ValidateRange(1, 4096)]
    [int]$SamplesPerFrame = 256,

    [ValidateRange(1, 32)]
    [int]$BlockIterations = 10,

    [ValidateRange(1, 32)]
    [int]$ColorIterations = 10,

    [ValidateRange(16, 65536)]
    [int]$ColorsPerCluster = 8192,

    [bool]$ActivePalettes = $true,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$ReportFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$cpuEncoder = Join-Path $repo "build\msvc\bpv1enc.exe"
$cudaEncoder = Join-Path $repo "build\msvc\bpv1enc_cuda.exe"
$encoder = if ($Device -eq "Cpu") {
    $cpuEncoder
}
elseif (Test-Path -LiteralPath $cudaEncoder) {
    $cudaEncoder
}
else {
    $cpuEncoder
}

$InputFile = [IO.Path]::GetFullPath($InputFile)
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
if (-not $ReportFile) {
    $ReportFile = [IO.Path]::ChangeExtension($OutputFile, ".json")
}
$ReportFile = [IO.Path]::GetFullPath($ReportFile)

if (($Width -band 1) -or ($Height -band 1)) {
    throw "BPV YUV420 dimensions must be even: ${Width}x${Height}."
}
if (-not (Test-Path -LiteralPath $InputFile)) {
    throw "Input video is missing: $InputFile"
}
if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if ($Device -eq "Cuda" -and
    -not (Test-Path -LiteralPath $cudaEncoder)) {
    & (Join-Path $PSScriptRoot "build_bpv_cuda.ps1")
    $encoder = $cudaEncoder
}
elseif (-not (Test-Path -LiteralPath $encoder)) {
    & (Join-Path $PSScriptRoot "build_bpv_msvc.ps1")
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $encoder)) {
    throw "FFmpeg or bpv1enc is unavailable."
}

$temporaryDirectory = Join-Path $repo ".tmp"
$outputParent = Split-Path $OutputFile -Parent
$reportParent = Split-Path $ReportFile -Parent
New-Item -ItemType Directory -Force -Path $temporaryDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
New-Item -ItemType Directory -Force -Path $reportParent | Out-Null
$identifier = [guid]::NewGuid().ToString("N")
$temporaryVideo = Join-Path $temporaryDirectory "bpv1-$identifier.y4m"
$temporaryAudio = Join-Path $temporaryDirectory "bpv1-$identifier.u8"

# This is the project's primary audio level curve: no EQ, loudness filter,
# standalone volume stage or limiter. The gentle compressor raises quiet
# material, and its measured makeup gain brings the resulting peak to -0.1 dBFS.
$audioConversion = "aformat=channel_layouts=mono,aresample=16000"
$audioLevelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
    "attack=0.01:release=250:knee=8:" +
    "link=maximum:detection=peak"
$audioPeakTargetDb = -0.1
if ($ResizeMode -eq "Crop") {
    $videoFilter = (
        "scale=${Width}:${Height}:force_original_aspect_ratio=increase:" +
        "force_divisible_by=2:flags=lanczos," +
        "crop=${Width}:${Height}:(iw-${Width})/2:(ih-${Height})/2," +
        "setsar=1,format=yuv420p"
    )
}
else {
    $videoFilter = (
        "scale=${Width}:${Height}:flags=lanczos," +
        "setsar=1,format=yuv420p"
    )
}

try {
    Write-Host "Measuring the primary audio level curve..."
    $analysisFilter = (
        "$audioConversion,$audioLevelCurve," +
        "astats=metadata=0:reset=0"
    )
    $analysisOutput = & $ffmpeg -hide_banner -nostats -i $InputFile `
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
        throw "FFmpeg did not report the processed audio peak."
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
            "The audio curve needs {0:N3} dB attenuation, but the " +
            "compressor makeup stage cannot attenuate."
        ) -f $curveMakeupDb
        throw $attenuationMessage
    }
    $curveMakeupText = $curveMakeupDb.ToString("0.000", $culture)
    $audioFilter = (
        "$audioConversion,${audioLevelCurve}:" +
        "makeup=${curveMakeupText}dB"
    )
    $audioStatusMessage = (
        "Preparing PCM_U8 mono 16 kHz: curve peak {0:N2} dBFS, " +
        "makeup {1} dB, target {2:N1} dBFS..."
    ) -f $curvePeakDb, $curveMakeupText, $audioPeakTargetDb
    Write-Host $audioStatusMessage
    & $ffmpeg -y -hide_banner -loglevel error -i $InputFile `
        -map 0:a:0 -vn -af $audioFilter `
        -ac 1 -ar 16000 -f u8 $temporaryAudio
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg audio conversion failed with exit code $LASTEXITCODE."
    }

    $videoArguments = @(
        "-y", "-hide_banner", "-loglevel", "warning", "-stats",
        "-i", $InputFile,
        "-map", "0:v:0", "-an",
        "-vf", $videoFilter,
        "-fps_mode", "cfr"
    )
    if ($MaxFrames) {
        $videoArguments += @("-frames:v", $MaxFrames)
    }
    $videoArguments += @(
        "-pix_fmt", "yuv420p",
        "-f", "yuv4mpegpipe",
        $temporaryVideo
    )
    Write-Host (
        "Preparing exact ${Width}x${Height} YUV420 at native FPS " +
        "using $ResizeMode resize..."
    )
    & $ffmpeg @videoArguments
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg video conversion failed with exit code $LASTEXITCODE."
    }

    $encoderArguments = @(
        $temporaryVideo,
        $OutputFile,
        "--threads", $Threads,
        "--device", $Device.ToLowerInvariant(),
        "--gop", $Gop,
        "--min-gop", $MinGop,
        "--scene-threshold",
            $SceneThreshold.ToString("0.######", $culture),
        "--lambda", $Lambda,
        "--candidate-palettes", $CandidatePalettes,
        "--search-radius", 2,
        "--max-block-dictionary", 256,
        "--sample-blocks", $SampleBlocks,
        "--samples-per-frame", $SamplesPerFrame,
        "--block-iterations", $BlockIterations,
        "--color-iterations", $ColorIterations,
        "--colors-per-cluster", $ColorsPerCluster,
        "--audio-u8", $temporaryAudio,
        "--audio-rate", 16000,
        "--report", $ReportFile,
        "--force"
    )
    if ($ActivePalettes) {
        $encoderArguments += "--active-palettes"
    }
    else {
        $encoderArguments += "--fixed-palettes"
    }
    $bpvVersion = 6
    $paletteMode = if ($ActivePalettes) {
        "active GOP palettes"
    }
    else {
        "one fixed palette bank"
    }
    Write-Host (
        "Encoding BPV1 v${bpvVersion}: ${Width}x${Height}, native FPS, " +
        "PCM_U8 mono 16 kHz, lambda $Lambda, GOP $MinGop..$Gop, " +
        "scene threshold $SceneThreshold, " +
        "$paletteMode, " +
        "$CandidatePalettes candidate palettes, $SampleBlocks training " +
        "blocks, $Threads worker threads, $Device device..."
    )
    & $encoder @encoderArguments
    if ($LASTEXITCODE -ne 0) {
        throw "BPV1 encoding failed with exit code $LASTEXITCODE."
    }

    $result = Get-Item -LiteralPath $OutputFile
    Write-Host ("Ready: {0} ({1:N0} bytes)" -f
        $result.FullName, $result.Length)
    Write-Host "Report: $ReportFile"
}
finally {
    if (Test-Path -LiteralPath $temporaryVideo) {
        Remove-Item -LiteralPath $temporaryVideo -Force
    }
    if (Test-Path -LiteralPath $temporaryAudio) {
        Remove-Item -LiteralPath $temporaryAudio -Force
    }
}
