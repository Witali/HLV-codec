#requires -Version 7.4

<#
.SYNOPSIS
Encodes several videos to BPV1 v6 while targeting an RGB PSNR value.

.DESCRIPTION
Prepares every input once, searches the BPV rate-distortion lambda against the
native encoder's measured rgbPsnrDb, and keeps the smallest tested file that
meets TargetPsnrDb within ToleranceDb. If the target is unattainable at
lambda=0, the maximum-quality result is retained with a warning.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string[]]$InputFile,

    [Parameter(Mandatory)]
    [ValidateRange(10.0, 99.0)]
    [double]$TargetPsnrDb,

    [string]$OutputDirectory,

    [string[]]$OutputName,

    [ValidateRange(2, 65534)]
    [int]$Width = 320,

    [ValidateRange(2, 65534)]
    [int]$Height = 240,

    [ValidateSet("Stretch", "Crop")]
    [string]$ResizeMode = "Stretch",

    [ValidateRange(0.0, 240.0)]
    [double]$Fps = 0,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateSet("Cpu", "Auto", "Cuda")]
    [string]$Device = "Cuda",

    [ValidateRange(1, 65535)]
    [int]$Gop = 48,

    [ValidateRange(1, 65535)]
    [int]$MinGop = 12,

    [ValidateRange(0.0, 1.0)]
    [double]$SceneThreshold = 0.35,

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

    [switch]$PixelMotion,

    [ValidateRange(0.0, 10.0)]
    [double]$ToleranceDb = 0.10,

    [ValidateRange(0.01, 1000000000)]
    [double]$InitialLambda = 64,

    [ValidateRange(0.01, 1000000000)]
    [double]$MaximumLambda = 65536,

    [ValidateRange(1, 32)]
    [int]$SearchIterations = 10,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [switch]$NoAudio,

    [string]$SummaryFile,

    [switch]$NoSummary,

    [switch]$Force
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
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
$approvedBunnySource = Join-Path $repo (
    "out\sources\big_buck_bunny_1080p_h264\" +
    "big_buck_bunny_1080p_h264.mov"
)
$culture = [Globalization.CultureInfo]::InvariantCulture

if (($Width -band 1) -or ($Height -band 1)) {
    throw "BPV YUV420 dimensions must be even: ${Width}x${Height}."
}
if ($InitialLambda -gt $MaximumLambda) {
    throw "InitialLambda must not exceed MaximumLambda."
}
if ($OutputName -and $OutputName.Count -ne $InputFile.Count) {
    throw "OutputName must contain one name for every InputFile."
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repo "out\BPV"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
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
    -not (Test-Path -LiteralPath $ffprobe) -or
    -not (Test-Path -LiteralPath $encoder)) {
    throw "FFmpeg, FFprobe or bpv1enc is unavailable."
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$temporaryRoot = Join-Path $repo ".tmp"
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null

$targetText = $TargetPsnrDb.ToString("0.##", $culture)
$targetFileText = $targetText.Replace(".", "p")
$bpvVersion = if ($PixelMotion) { 7 } else { 6 }
if (-not $NoSummary) {
    if (-not $SummaryFile) {
        $SummaryFile = Join-Path $OutputDirectory (
            "BPVv${bpvVersion}_target_${targetFileText}dB_summary.json"
        )
    }
    $SummaryFile = [IO.Path]::GetFullPath($SummaryFile)
    if ((Test-Path -LiteralPath $SummaryFile) -and -not $Force) {
        throw "Summary file already exists; use -Force: $SummaryFile"
    }
    $summaryParent = Split-Path $SummaryFile -Parent
    New-Item -ItemType Directory -Force -Path $summaryParent | Out-Null
}

$resolvedOutputNames = @(
    for (
        $nameIndex = 0;
        $nameIndex -lt $InputFile.Count;
        $nameIndex++
    ) {
        if ($OutputName) {
            $OutputName[$nameIndex]
        }
        else {
            [IO.Path]::GetFileNameWithoutExtension(
                $InputFile[$nameIndex]
            )
        }
    }
)
$duplicateOutputNames = @(
    $resolvedOutputNames |
        Group-Object { $_.ToLowerInvariant() } |
        Where-Object Count -gt 1
)
if ($duplicateOutputNames.Count) {
    throw (
        "Output names must be unique: " +
        (($duplicateOutputNames | ForEach-Object Name) -join ", ")
    )
}

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

function Invoke-BpvQualityTrial {
    param(
        [double]$Lambda,
        [string]$PreparedVideo,
        [string]$PreparedAudio,
        [string]$WorkingDirectory,
        [hashtable]$Cache,
        [Collections.Generic.List[object]]$Trials,
        [pscustomobject]$State
    )

    $lambdaText = $Lambda.ToString("0.########", $culture)
    if ($Cache.ContainsKey($lambdaText)) {
        return $Cache[$lambdaText]
    }

    $State.Trial++
    $trialStem = "trial-{0:D2}-lambda-{1}" -f (
        $State.Trial,
        $lambdaText.Replace(".", "p")
    )
    $trialOutput = Join-Path $WorkingDirectory "${trialStem}.bpv1"
    $trialReport = Join-Path $WorkingDirectory "${trialStem}.json"
    $arguments = @(
        $PreparedVideo,
        $trialOutput,
        "--threads", $Threads,
        "--device", $Device.ToLowerInvariant(),
        "--gop", $Gop,
        "--min-gop", $MinGop,
        "--scene-threshold",
            $SceneThreshold.ToString("0.######", $culture),
        "--lambda", $lambdaText,
        "--candidate-palettes", $CandidatePalettes,
        "--search-radius", 2,
        "--max-block-dictionary", 256,
        "--sample-blocks", $SampleBlocks,
        "--samples-per-frame", $SamplesPerFrame,
        "--block-iterations", $BlockIterations,
        "--color-iterations", $ColorIterations,
        "--colors-per-cluster", $ColorsPerCluster,
        "--report", $trialReport,
        "--force",
        "--no-progress"
    )
    if ($PreparedAudio) {
        $arguments += @(
            "--audio-ima-s16le", $PreparedAudio,
            "--audio-rate", 32000
        )
    }
    if ($ActivePalettes) {
        $arguments += "--active-palettes"
    }
    else {
        $arguments += "--fixed-palettes"
    }
    if ($PixelMotion) {
        $arguments += "--pixel-motion"
    }

    Write-Host "  Trial $($State.Trial): lambda=$lambdaText"
    $encoderOutput = & $encoder @arguments 2>&1
    $encoderExitCode = $LASTEXITCODE
    if ($encoderExitCode -ne 0) {
        throw (
            "BPV1 quality trial failed with exit code " +
            "${encoderExitCode}:`n" +
            ($encoderOutput -join "`n")
        )
    }
    $report = Get-Content -LiteralPath $trialReport -Raw |
        ConvertFrom-Json
    $lossless = [double]$report.rgbMse -eq 0.0
    $result = [pscustomobject]@{
        Lambda = $Lambda
        PsnrDb = if ($lossless) {
            [double]::PositiveInfinity
        }
        else {
            [double]$report.rgbPsnrDb
        }
        Lossless = $lossless
        Bytes = [long]$report.bytes
        OutputFile = $trialOutput
        ReportFile = $trialReport
        Report = $report
    }
    $qualityDisplay = if ($result.Lossless) {
        "lossless"
    }
    else {
        "{0:N3} dB" -f $result.PsnrDb
    }
    Write-Host ("    {0}, {1:N0} bytes" -f
        $qualityDisplay, $result.Bytes)
    $Cache[$lambdaText] = $result
    [void]$Trials.Add($result)
    return $result
}

$summary = [Collections.Generic.List[object]]::new()
$approvedBunnyFullPath = [IO.Path]::GetFullPath($approvedBunnySource)

for ($inputIndex = 0; $inputIndex -lt $InputFile.Count; $inputIndex++) {
    $source = [IO.Path]::GetFullPath($InputFile[$inputIndex])
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Input video is missing: $source"
    }
    if ($source -match "(?i)big[_ .-]*buck|bigbuckbunny" -and
        -not $source.Equals(
            $approvedBunnyFullPath,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        throw (
            "Big Buck Bunny must be transcoded only from the approved " +
            "1080p MOV: $approvedBunnyFullPath"
        )
    }

    $baseName = $resolvedOutputNames[$inputIndex]
    if ([string]::IsNullOrWhiteSpace($baseName) -or
        $baseName.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "Invalid output name: $baseName"
    }

    $workingDirectory = Join-Path $temporaryRoot (
        "bpv-target-" + [guid]::NewGuid().ToString("N")
    )
    $preparedVideo = Join-Path $workingDirectory "input.y4m"
    $preparedAudio = $null
    $trials = [Collections.Generic.List[object]]::new()
    $cache = @{}
    $state = [pscustomobject]@{ Trial = 0 }
    New-Item -ItemType Directory -Path $workingDirectory | Out-Null

    try {
        Write-Host ""
        Write-Host "Preparing: $source"
        $videoArguments = @(
            "-y", "-hide_banner", "-loglevel", "error", "-nostats",
            "-i", $source,
            "-map", "0:v:0", "-an",
            "-vf", $videoFilter
        )
        if ($Fps) {
            $videoArguments += @(
                "-r", $Fps.ToString("0.########", $culture)
            )
        }
        $videoArguments += @("-fps_mode", "cfr")
        if ($MaxFrames) {
            $videoArguments += @("-frames:v", $MaxFrames)
        }
        $videoArguments += @(
            "-pix_fmt", "yuv420p",
            "-f", "yuv4mpegpipe",
            $preparedVideo
        )
        & $ffmpeg @videoArguments
        if ($LASTEXITCODE -ne 0) {
            throw "FFmpeg video conversion failed with exit code $LASTEXITCODE."
        }

        if (-not $NoAudio) {
            $audioStreams = & $ffprobe -v error `
                -select_streams a:0 `
                -show_entries stream=index `
                -of "csv=p=0" `
                $source
            if ($LASTEXITCODE -ne 0) {
                throw "FFprobe audio inspection failed."
            }
            if ($audioStreams) {
                $preparedAudio = Join-Path $workingDirectory "audio.s16le"
                $audioConversion = (
                    "aformat=channel_layouts=mono,aresample=32000"
                )
                $audioLevelCurve = (
                    "acompressor=threshold=-20dB:ratio=1.6:" +
                    "attack=0.01:release=250:knee=8:" +
                    "link=maximum:detection=peak"
                )
                $analysisFilter = (
                    "$audioConversion,$audioLevelCurve," +
                    "astats=metadata=0:reset=0"
                )
                $analysisOutput = & $ffmpeg -hide_banner -nostats `
                    -i $source -map 0:a:0 -vn `
                    -af $analysisFilter -ac 1 -ar 32000 `
                    -f null NUL 2>&1
                if ($LASTEXITCODE -ne 0) {
                    throw "FFmpeg audio analysis failed."
                }
                $peakMatches = [regex]::Matches(
                    ($analysisOutput -join "`n"),
                    "Peak level dB:\s*(-?\d+(?:\.\d+)?)"
                )
                if (-not $peakMatches.Count) {
                    throw "FFmpeg did not report the processed audio peak."
                }
                $curvePeakDb = (
                    $peakMatches |
                        ForEach-Object {
                            [double]::Parse(
                                $_.Groups[1].Value,
                                $culture
                            )
                        } |
                        Measure-Object -Maximum
                ).Maximum
                $makeupDb = -0.1 - $curvePeakDb
                if ($makeupDb -lt 0.0) {
                    throw (
                        "The audio curve needs attenuation, but the " +
                        "compressor makeup stage cannot attenuate."
                    )
                }
                $makeupText = $makeupDb.ToString("0.000", $culture)
                $audioFilter = (
                    "$audioConversion,${audioLevelCurve}:" +
                    "makeup=${makeupText}dB"
                )
                & $ffmpeg -y -hide_banner -loglevel error `
                    -i $source -map 0:a:0 -vn `
                    -af $audioFilter -ac 1 -ar 32000 `
                    -f s16le $preparedAudio
                if ($LASTEXITCODE -ne 0) {
                    throw "FFmpeg audio conversion failed."
                }
            }
        }

        Write-Host (
            "Searching for ${targetText} dB " +
            "(tolerance ${ToleranceDb} dB)..."
        )
        $lowestLambda = Invoke-BpvQualityTrial `
            -Lambda 0 `
            -PreparedVideo $preparedVideo `
            -PreparedAudio $preparedAudio `
            -WorkingDirectory $workingDirectory `
            -Cache $cache `
            -Trials $trials `
            -State $state

        if ($lowestLambda.PsnrDb -lt
            $TargetPsnrDb - $ToleranceDb) {
            $selected = $lowestLambda
            Write-Warning (
                "Target ${targetText} dB is unattainable; maximum is " +
                ("{0:N3} dB at lambda=0." -f $selected.PsnrDb)
            )
        }
        else {
            $lower = $lowestLambda
            $upperLambda = $InitialLambda
            $upper = Invoke-BpvQualityTrial `
                -Lambda $upperLambda `
                -PreparedVideo $preparedVideo `
                -PreparedAudio $preparedAudio `
                -WorkingDirectory $workingDirectory `
                -Cache $cache `
                -Trials $trials `
                -State $state

            while ($upper.PsnrDb -gt $TargetPsnrDb - $ToleranceDb -and
                   $upperLambda -lt $MaximumLambda) {
                $lower = $upper
                $upperLambda = [Math]::Min(
                    $MaximumLambda,
                    $upperLambda * 2.0
                )
                $upper = Invoke-BpvQualityTrial `
                    -Lambda $upperLambda `
                    -PreparedVideo $preparedVideo `
                    -PreparedAudio $preparedAudio `
                    -WorkingDirectory $workingDirectory `
                    -Cache $cache `
                    -Trials $trials `
                    -State $state
            }

            if ($upper.PsnrDb -le
                $TargetPsnrDb - $ToleranceDb) {
                for ($iteration = 0;
                     $iteration -lt $SearchIterations;
                     $iteration++) {
                    $middleLambda = (
                        $lower.Lambda + $upper.Lambda
                    ) / 2.0
                    if ([Math]::Abs(
                        $upper.Lambda - $lower.Lambda
                    ) -lt 0.01) {
                        break
                    }
                    $middle = Invoke-BpvQualityTrial `
                        -Lambda $middleLambda `
                        -PreparedVideo $preparedVideo `
                        -PreparedAudio $preparedAudio `
                        -WorkingDirectory $workingDirectory `
                        -Cache $cache `
                        -Trials $trials `
                        -State $state
                    if ($middle.PsnrDb -ge
                        $TargetPsnrDb - $ToleranceDb) {
                        $lower = $middle
                    }
                    else {
                        $upper = $middle
                    }
                }
            }

            $acceptable = @(
                $trials |
                    Where-Object {
                        $_.PsnrDb -ge
                            $TargetPsnrDb - $ToleranceDb
                    } |
                    Sort-Object `
                        @{ Expression = "Bytes"; Ascending = $true },
                        @{
                            Expression = {
                                [Math]::Abs(
                                    $_.PsnrDb - $TargetPsnrDb
                                )
                            }
                            Ascending = $true
                        }
            )
            if ($acceptable.Count) {
                $selected = $acceptable[0]
            }
            else {
                $selected = $trials |
                    Sort-Object PsnrDb -Descending |
                    Select-Object -First 1
            }
        }

        $fps = (
            [double]$selected.Report.fpsNumerator /
            [double]$selected.Report.fpsDenominator
        )
        $roundedFps = [Math]::Round($fps)
        $fpsText = if ([Math]::Abs($fps - $roundedFps) -lt 0.0005) {
            $roundedFps.ToString("0", $culture)
        }
        else {
            $fps.ToString("0.###", $culture).Replace(".", "p")
        }
        $qualitySuffix = if ($selected.Lossless) {
            "lossless"
        }
        else {
            $actualQuality = [Math]::Round($selected.PsnrDb)
            $qualityText = $actualQuality.ToString("0", $culture)
            "${qualityText}dB"
        }
        $outputStem = (
            "${baseName}_${Width}x${Height}_${fpsText}fps_" +
            "BPVv${bpvVersion}_${qualitySuffix}"
        )
        $outputPath = Join-Path $OutputDirectory "${outputStem}.bpv1"
        $reportPath = Join-Path $OutputDirectory "${outputStem}.json"
        foreach ($destination in @($outputPath, $reportPath)) {
            if ((Test-Path -LiteralPath $destination) -and -not $Force) {
                throw "Output already exists; use -Force: $destination"
            }
        }

        Move-Item -LiteralPath $selected.OutputFile `
            -Destination $outputPath -Force
        Move-Item -LiteralPath $selected.ReportFile `
            -Destination $reportPath -Force

        $finalReport = Get-Content -LiteralPath $reportPath -Raw |
            ConvertFrom-Json
        $finalReport.input = $source
        $finalReport.output = $outputPath
        $trialSummary = @(
            $trials |
                Sort-Object Lambda |
                ForEach-Object {
                    [pscustomobject]@{
                        lambda = $_.Lambda
                        rgbPsnrDb = if ($_.Lossless) {
                            $null
                        }
                        else {
                            $_.PsnrDb
                        }
                        lossless = $_.Lossless
                        bytes = $_.Bytes
                    }
                }
        )
        $qualitySearch = [pscustomobject]@{
            targetPsnrDb = $TargetPsnrDb
            toleranceDb = $ToleranceDb
            selectedLambda = $selected.Lambda
            selectedPsnrDb = if ($selected.Lossless) {
                $null
            }
            else {
                $selected.PsnrDb
            }
            lossless = $selected.Lossless
            maxFrames = $MaxFrames
            trials = $trialSummary
        }
        $finalReport |
            Add-Member -NotePropertyName qualitySearch `
                -NotePropertyValue $qualitySearch -Force
        $finalReport |
            ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM

        $item = [pscustomobject]@{
            input = $source
            output = $outputPath
            report = $reportPath
            targetPsnrDb = $TargetPsnrDb
            actualPsnrDb = if ($selected.Lossless) {
                $null
            }
            else {
                $selected.PsnrDb
            }
            lossless = $selected.Lossless
            lambda = $selected.Lambda
            bytes = $selected.Bytes
            frames = [long]$selected.Report.frames
            fps = $fps
            audio = [bool]$preparedAudio
            targetReached = (
                $selected.PsnrDb -ge
                $TargetPsnrDb - $ToleranceDb
            )
        }
        [void]$summary.Add($item)
        $selectedDisplay = if ($selected.Lossless) {
            "lossless"
        }
        else {
            "{0:N3} dB" -f $selected.PsnrDb
        }
        Write-Host (
            "Selected lambda={0}: {1}, {2:N0} bytes" -f
            $selected.Lambda, $selectedDisplay, $selected.Bytes
        )
        Write-Host "Ready: $outputPath"
    }
    finally {
        if (Test-Path -LiteralPath $workingDirectory) {
            Remove-Item -LiteralPath $workingDirectory `
                -Recurse -Force
        }
    }
}

if (-not $NoSummary) {
    $summaryJson = ConvertTo-Json -InputObject @($summary) -Depth 6
    $summaryJson |
        Set-Content -LiteralPath $SummaryFile -Encoding utf8NoBOM
    Write-Host ""
    Write-Host "Summary: $SummaryFile"
}
$summary
