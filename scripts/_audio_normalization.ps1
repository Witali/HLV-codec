#requires -Version 7.4

function Get-PrimaryAudioSampleRate {
    param(
        [Parameter(Mandatory)]
        [string]$Ffprobe,

        [Parameter(Mandatory)]
        [string]$InputFile,

        [ValidateRange(8000, 48000)]
        [int]$MinimumRate = 8000,

        [ValidateRange(8000, 48000)]
        [int]$MaximumRate = 48000
    )

    $rateLines = & $Ffprobe -v error -select_streams a:0 `
        -show_entries stream=sample_rate -of csv=p=0 $InputFile
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe audio sample-rate inspection failed."
    }
    $rateText = $rateLines | Select-Object -First 1
    $rate = 0
    if (-not [int]::TryParse(([string]$rateText).Trim(), [ref]$rate) -or
        $rate -lt $MinimumRate -or $rate -gt $MaximumRate) {
        throw (
            "The primary audio sample rate must be within " +
            "${MinimumRate}..${MaximumRate} Hz."
        )
    }
    $rate
}

function Get-PeakSafeAudioFilter {
    param(
        [Parameter(Mandatory)]
        [string]$Ffmpeg,

        [Parameter(Mandatory)]
        [string]$InputFile,

        [Parameter(Mandatory)]
        [ValidateRange(8000, 48000)]
        [int]$Rate,

        [ValidateRange(-3.0, -0.01)]
        [double]$TargetPeakDb = -0.1
    )

    # This is the project's primary audio level curve. Keep every production
    # transcoder on this helper so container and codec changes cannot silently
    # produce a different listening level.
    $conversion = "aformat=channel_layouts=mono,aresample=$Rate"
    $levelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
        "attack=0.01:release=250:knee=8:" +
        "link=maximum:detection=peak"
    $analysisFilter = (
        "$conversion,$levelCurve,astats=metadata=0:reset=0"
    )

    Write-Host "Measuring the primary audio level curve..."
    $analysisOutput = & $Ffmpeg -hide_banner -nostats -i $InputFile `
        -map 0:a:0 -vn -af $analysisFilter `
        -ac 1 -ar $Rate -f null NUL 2>&1
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
        OutputGainText = $makeupText
        TargetPeakDb = $TargetPeakDb
        Rate = $Rate
    }
}

function Write-AudioNormalizationStatus {
    param(
        [Parameter(Mandatory)]
        [psobject]$Normalization
    )

    $status = (
        (
        "Audio curve: peak {0:N2} dBFS, makeup {1} dB, " +
        "target {2:N1} dBFS"
        ) -f
        $Normalization.CurvePeakDb,
        $Normalization.OutputGainText,
        $Normalization.TargetPeakDb
    )
    Write-Host $status
}
