#requires -Version 7.4

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$work = Join-Path $repo ".tmp\transcode-wrapper-tests"
$source = Join-Path $work "wrapper-source.mkv"

function Assert-ProbedValue {
    param(
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][string]$Entries,
        [Parameter(Mandatory)][string]$Expected
    )

    $actual = & $ffprobe -v error -select_streams v:0 `
        -show_entries "stream=$Entries" `
        -of default=nw=1:nk=1 $File
    if ($LASTEXITCODE -ne 0 -or ($actual -join "`n") -notmatch $Expected) {
        throw "Unexpected metadata for $File`: $($actual -join ', ')"
    }
}

function Assert-NormalizedAudio {
    param(
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][string]$Codec,
        [Parameter(Mandatory)][int]$Rate,
        [double]$PeakToleranceDb = 0.25
    )

    $audioText = & $ffprobe -v error -select_streams a:0 `
        -show_entries stream=codec_name,sample_rate,channels `
        -of json $File
    $audioStreams = if ($LASTEXITCODE -eq 0) {
        @(($audioText | ConvertFrom-Json).streams)
    }
    else {
        @()
    }
    if ($audioStreams.Count -ne 1 -or
        $audioStreams[0].codec_name -ne $Codec -or
        [int]$audioStreams[0].sample_rate -ne $Rate -or
        $audioStreams[0].channels -ne 1) {
        throw "Unexpected normalized audio profile for $File."
    }

    $peakOutput = & $ffmpeg -hide_banner -nostats -i $File `
        -map 0:a:0 -vn -af "astats=metadata=0:reset=0" `
        -f null NUL 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not measure normalized audio in $File."
    }
    $peakMatches = [regex]::Matches(
        ($peakOutput -join "`n"),
        "Peak level dB:\s*(-?\d+(?:\.\d+)?)"
    )
    if (-not $peakMatches.Count) {
        throw "FFmpeg did not report the output audio peak for $File."
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $peakDb = (
        $peakMatches |
            ForEach-Object {
                [double]::Parse($_.Groups[1].Value, $culture)
            } |
            Measure-Object -Maximum
    ).Maximum
    if ([Math]::Abs($peakDb - (-0.1)) -gt $PeakToleranceDb) {
        throw (
            "Audio peak in $File is ${peakDb} dBFS; expected " +
            "-0.1 +/- ${PeakToleranceDb} dB."
        )
    }
}

try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    & $ffmpeg -y -hide_banner -loglevel error `
        -f lavfi -i "testsrc2=size=640x360:rate=30:duration=1" `
        -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=1" `
        -map 0:v:0 -map 1:a:0 -c:v ffv1 -c:a pcm_s16le $source
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create the wrapper test source."
    }

    & (Join-Path $PSScriptRoot "transcode_h263.ps1") $source `
        -OutputFile (Join-Path $work "h263.avi") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_mpeg4_simple.ps1") $source `
        -OutputFile (Join-Path $work "mpeg4-simple.avi") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_mjpeg.ps1") $source `
        -OutputFile (Join-Path $work "mjpeg.avi") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_mpeg1.ps1") $source `
        -OutputFile (Join-Path $work "mpeg1.mpg") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_divx3.ps1") $source `
        -OutputFile (Join-Path $work "divx3.avi") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_hlv14.ps1") $source `
        -OutputFile (Join-Path $work "hlv14.hlv") `
        -MaxFrames 2 -Force
    & (Join-Path $PSScriptRoot "transcode_bpv6.ps1") $source `
        -OutputDirectory (Join-Path $work "BPV") `
        -Width 320 -Height 180 -MaxFrames 2 -Force
    & (Join-Path $PSScriptRoot "transcode_bpv6.ps1") $source `
        -OutputDirectory (Join-Path $work "BPV7") `
        -Width 320 -Height 180 -MaxFrames 2 -NoAudio -PixelMotion -Force

    Assert-ProbedValue -File (Join-Path $work "h263.avi") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)h263.*352.*288"
    Assert-ProbedValue -File (Join-Path $work "mpeg4-simple.avi") `
        -Entries "codec_name,profile,codec_tag_string,width,height,has_b_frames" `
        -Expected "(?s)mpeg4.*Simple Profile.*M4S2.*320.*240.*0"
    Assert-ProbedValue -File (Join-Path $work "mjpeg.avi") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)mjpeg.*320.*240"
    Assert-ProbedValue -File (Join-Path $work "mpeg1.mpg") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)mpeg1video.*320.*240"
    Assert-ProbedValue -File (Join-Path $work "divx3.avi") `
        -Entries "codec_name,codec_tag_string,r_frame_rate" `
        -Expected "(?s)msmpeg4v3.*DIV3.*15/1"
    Assert-NormalizedAudio -File (Join-Path $work "h263.avi") `
        -Codec "pcm_s16le" -Rate 8000
    Assert-NormalizedAudio -File (Join-Path $work "mpeg4-simple.avi") `
        -Codec "pcm_s16le" -Rate 8000
    Assert-NormalizedAudio -File (Join-Path $work "mjpeg.avi") `
        -Codec "pcm_u8" -Rate 16000
    Assert-NormalizedAudio -File (Join-Path $work "mpeg1.mpg") `
        -Codec "mp2" -Rate 32000 -PeakToleranceDb 1.5
    Assert-NormalizedAudio -File (Join-Path $work "divx3.avi") `
        -Codec "pcm_u8" -Rate 16000

    foreach ($required in @(
        (Join-Path $work "divx3.json"),
        (Join-Path $work "hlv14.hlv"),
        (Join-Path $work "hlv14.json"),
        (Join-Path $work "hlv14.cq.csv")
    )) {
        if (-not (Test-Path -LiteralPath $required)) {
            throw "Wrapper did not create required output: $required"
        }
    }
    $hlvReport = Get-Content -LiteralPath (
        Join-Path $work "hlv14.json"
    ) -Raw | ConvertFrom-Json
    if ($hlvReport.audio -ne "PCM_U8 mono 16000 Hz" -or
        $hlvReport.audioNormalization.curve -ne
            "primary-compressor-peak" -or
        [Math]::Abs(
            $hlvReport.audioNormalization.targetPeakDb - (-0.1)
        ) -gt 0.000001) {
        throw "HLV wrapper did not record normalized 16 kHz audio."
    }
    $bpvFiles = @(Get-ChildItem -LiteralPath (Join-Path $work "BPV") `
        -Filter "*.bpv1")
    if ($bpvFiles.Count -ne 1) {
        throw "BPV wrapper did not create exactly one output video."
    }
    $bpvInfo = & node (Join-Path $repo "codecs\bpv\tools\bpv1info.js") `
        $bpvFiles[0].FullName --json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or
        $bpvInfo.version -ne 6 -or
        $bpvInfo.audioSampleRate -ne 16000 -or
        $bpvInfo.audioChannels -ne 1 -or
        $bpvInfo.audioBytes -le 0 -or
        $bpvInfo.maxPatternDictionary -ne 0 -or
        @($bpvInfo.modeCounts.PSObject.Properties).Count -ne 4) {
        throw "BPV wrapper did not create a valid four-mode BPV v6 stream."
    }
    $bpvReportPath = [IO.Path]::ChangeExtension(
        $bpvFiles[0].FullName,
        ".json"
    )
    $bpvReport = Get-Content -LiteralPath $bpvReportPath -Raw |
        ConvertFrom-Json
    if ($bpvReport.computeBackend -ne "cuda" -or
        $bpvReport.samplesPerFrame -ne 256 -or
        $bpvReport.minimumGop -ne 12 -or
        $bpvReport.candidatePaletteCount -ne 8 -or
        $bpvReport.paletteSearch -ne "rgb-lut" -or
        $bpvReport.paletteIndexBitsPerChannel -ne 4 -or
        [Math]::Abs($bpvReport.sceneThreshold - 0.35) -gt 0.000001) {
        throw (
            "BPV wrapper did not use the CUDA production " +
            "palette/scene defaults."
        )
    }

    $bpv7Files = @(Get-ChildItem -LiteralPath (Join-Path $work "BPV7") `
        -Filter "*BPVv7*.bpv1")
    if ($bpv7Files.Count -ne 1) {
        throw "BPV pixel-motion wrapper did not create one BPV v7 file."
    }
    $bpv7Info = & node `
        (Join-Path $repo "codecs\bpv\tools\bpv1info.js") `
        $bpv7Files[0].FullName --json | ConvertFrom-Json
    $bpv7Report = Get-Content -LiteralPath (
        [IO.Path]::ChangeExtension($bpv7Files[0].FullName, ".json")
    ) -Raw | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or
        $bpv7Info.version -ne 7 -or
        $bpv7Report.motionUnits -ne "pixels" -or
        $bpv7Report.computeBackend -ne "cuda") {
        throw "BPV pixel-motion wrapper did not use CUDA BPV v7."
    }

    Write-Host (
        "All seven production transcode formats use the normalized " +
        "audio profile; the BPV v7 pixel-motion variant also passed."
    )
}
finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $workspaceTmp = [IO.Path]::GetFullPath(
        (Join-Path $repo ".tmp")
    ) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedWork.StartsWith(
        $workspaceTmp,
        [StringComparison]::OrdinalIgnoreCase
    ) -and (Test-Path -LiteralPath $resolvedWork)) {
        Remove-Item -LiteralPath $resolvedWork -Recurse -Force
    }
}
