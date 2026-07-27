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
        -MaxFrames 3 -NoAudio -Force
    & (Join-Path $PSScriptRoot "transcode_mjpeg.ps1") $source `
        -OutputFile (Join-Path $work "mjpeg.avi") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_mpeg1.ps1") $source `
        -OutputFile (Join-Path $work "mpeg1.mpg") `
        -MaxFrames 3 -Force
    & (Join-Path $PSScriptRoot "transcode_divx3.ps1") $source `
        -OutputFile (Join-Path $work "divx3.avi") `
        -MaxFrames 3 -NoAudio -Force
    & (Join-Path $PSScriptRoot "transcode_hlv14.ps1") $source `
        -OutputFile (Join-Path $work "hlv14.hlv") `
        -MaxFrames 2 -NoAudio -Force
    & (Join-Path $PSScriptRoot "transcode_bpv6.ps1") $source `
        -OutputDirectory (Join-Path $work "BPV") `
        -Width 320 -Height 180 -MaxFrames 2 -NoAudio -Force

    Assert-ProbedValue -File (Join-Path $work "h263.avi") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)h263.*352.*288"
    Assert-ProbedValue -File (Join-Path $work "mjpeg.avi") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)mjpeg.*320.*240"
    Assert-ProbedValue -File (Join-Path $work "mpeg1.mpg") `
        -Entries "codec_name,width,height" `
        -Expected "(?s)mpeg1video.*320.*240"
    Assert-ProbedValue -File (Join-Path $work "divx3.avi") `
        -Entries "codec_name,codec_tag_string,r_frame_rate" `
        -Expected "(?s)msmpeg4v3.*DIV3.*15/1"

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
    $bpvFiles = @(Get-ChildItem -LiteralPath (Join-Path $work "BPV") `
        -Filter "*.bpv1")
    if ($bpvFiles.Count -ne 1) {
        throw "BPV wrapper did not create exactly one output video."
    }
    $bpvInfo = & node (Join-Path $repo "codecs\bpv\tools\bpv1info.js") `
        $bpvFiles[0].FullName --json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or
        $bpvInfo.version -ne 6 -or
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
    if ($bpvReport.samplesPerFrame -ne 256 -or
        $bpvReport.minimumGop -ne 12 -or
        $bpvReport.candidatePaletteCount -ne 8 -or
        $bpvReport.paletteSearch -ne "rgb-lut" -or
        $bpvReport.paletteIndexBitsPerChannel -ne 4 -or
        [Math]::Abs($bpvReport.sceneThreshold - 0.35) -gt 0.000001) {
        throw "BPV wrapper did not use production palette/scene defaults."
    }

    Write-Host "All six production transcode wrappers passed."
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
