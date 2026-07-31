#requires -Version 7.4

<#
.SYNOPSIS
Generates the picture-rich regression source and encodes it in every
production ESP32 video format.
#>
[CmdletBinding()]
param(
    [ValidateRange(8, 300)]
    [int]$Frames = 60,

    [ValidateRange(1, 30)]
    [int]$Fps = 30,

    [ValidatePattern("^[A-Za-z0-9][A-Za-z0-9._-]*$")]
    [string]$BaseName = "VideoFormatRegression",

    [string]$OutputRoot = (
        Join-Path (Split-Path $PSScriptRoot -Parent) "out"
    ),

    [ValidateRange(10.0, 99.0)]
    [double]$BpvTargetPsnrDb = 35.0,

    [ValidateRange(1, 16)]
    [int]$Threads = 6,

    [ValidateSet("Cpu", "Auto", "Cuda")]
    [string]$BpvDevice = "Auto",

    [switch]$Force
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "_transcode_profile_common.ps1")

$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$rate = Format-TranscodeNumber -Value $Fps
$divxFrames = [int][Math]::Ceiling($Frames / 2.0)
$divxRate = Format-TranscodeNumber -Value ($Fps / 2.0)
$sourceDirectory = Join-Path $OutputRoot "sources"
$source = Join-Path $sourceDirectory "${BaseName}.mkv"
$hlv = Join-Path $OutputRoot (
    "HLV\${BaseName}_320x240_${rate}fps_" +
    "HLVv15_adaptive35-42dB.hlv"
)
$mjpeg = Join-Path $OutputRoot (
    "MJPEG\${BaseName}_320x240_${rate}fps_MJPEG_q3.avi"
)
$divx3 = Join-Path $OutputRoot (
    "DivX3\${BaseName}_320x240_${divxRate}fps_DivX3_q3.avi"
)
$mpeg1 = Join-Path $OutputRoot (
    "MPEG1\${BaseName}_320x240_${rate}fps_MPEG1_q3.mpg"
)
$h263 = Join-Path $OutputRoot (
    "H263\${BaseName}_352x288_${rate}fps_H263_CIF_q6.avi"
)
$mpeg4 = Join-Path $OutputRoot (
    "MPEG4SP\${BaseName}_320x240_${rate}fps_MPEG4SP_35dB.avi"
)
$knownOutputs = @($hlv, $mjpeg, $divx3, $mpeg1, $h263, $mpeg4)

if (-not $Force) {
    foreach ($path in @($source) + $knownOutputs) {
        if (Test-Path -LiteralPath $path) {
            throw "Output already exists; use -Force to replace it: $path"
        }
    }
    $bpvPattern = (
        "${BaseName}_320x240_${rate}fps_BPVv6_*.bpv1"
    )
    $bpvDirectory = Join-Path $OutputRoot "BPV"
    if (Test-Path -LiteralPath $bpvDirectory) {
        $existingBpv = @(Get-ChildItem -LiteralPath $bpvDirectory `
            -Filter $bpvPattern)
        if ($existingBpv.Count) {
            throw (
                "A BPV output already exists; use -Force to replace it: " +
                $existingBpv[0].FullName
            )
        }
    }
}

New-Item -ItemType Directory -Force -Path $sourceDirectory | Out-Null
& (Join-Path $PSScriptRoot "python.ps1") `
    (Join-Path $PSScriptRoot "make_video_format_test_source.py") `
    --output $source --frames $Frames --fps $Fps
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the visual regression source."
}

$replace = @{}
if ($Force) { $replace.Force = $true }
& (Join-Path $PSScriptRoot "transcode_hlv15.ps1") $source `
    -OutputFile $hlv @replace
& (Join-Path $PSScriptRoot "transcode_mjpeg.ps1") $source `
    -OutputFile $mjpeg -Threads $Threads @replace
& (Join-Path $PSScriptRoot "transcode_divx3.ps1") $source `
    -OutputFile $divx3 -MaxFrames $divxFrames -Threads $Threads @replace
& (Join-Path $PSScriptRoot "transcode_mpeg1.ps1") $source `
    -OutputFile $mpeg1 -Threads $Threads @replace
& (Join-Path $PSScriptRoot "transcode_h263.ps1") $source `
    -OutputFile $h263 -Threads $Threads @replace
& (Join-Path $PSScriptRoot "transcode_mpeg4_simple.ps1") $source `
    -OutputFile $mpeg4 -Threads $Threads @replace

$bpvStartedUtc = [DateTime]::UtcNow
& (Join-Path $PSScriptRoot "transcode_bpv6.ps1") $source `
    -OutputDirectory (Join-Path $OutputRoot "BPV") `
    -Width 320 -Height 240 -TargetPsnrDb $BpvTargetPsnrDb `
    -Device $BpvDevice -Threads $Threads @replace

$bpvPattern = "${BaseName}_320x240_${rate}fps_BPVv6_*.bpv1"
$bpvCandidates = @(
    Get-ChildItem -LiteralPath (Join-Path $OutputRoot "BPV") `
        -Filter $bpvPattern |
        Where-Object {
            $_.LastWriteTimeUtc -ge $bpvStartedUtc.AddSeconds(-2)
        } |
        Sort-Object LastWriteTimeUtc -Descending
)
if (-not $bpvCandidates.Count) {
    throw "The BPV encoder did not produce a current output."
}
$bpv = $bpvCandidates[0].FullName

$clips = @(
    [ordered]@{ format = "HLV v15"; file = $hlv; frames = $Frames },
    [ordered]@{ format = "BPV v6"; file = $bpv; frames = $Frames },
    [ordered]@{ format = "MJPEG"; file = $mjpeg; frames = $Frames },
    [ordered]@{ format = "DivX 3"; file = $divx3; frames = $divxFrames },
    [ordered]@{ format = "MPEG-1"; file = $mpeg1; frames = $Frames },
    [ordered]@{ format = "H.263"; file = $h263; frames = $Frames },
    [ordered]@{ format = "MPEG-4 SP"; file = $mpeg4; frames = $Frames }
)
foreach ($clip in $clips) {
    if (-not (Test-Path -LiteralPath $clip.file)) {
        throw "Missing encoded clip: $($clip.file)"
    }
    $clip.bytes = (Get-Item -LiteralPath $clip.file).Length
}

$manifest = [ordered]@{
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    source = $source
    sourceFrames = $Frames
    sourceFps = $Fps
    clips = $clips
}
$manifestPath = Join-Path $sourceDirectory "${BaseName}_all_formats.json"
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host ""
Write-Host "Generated the test video in every production format:"
foreach ($clip in $clips) {
    Write-Host ("  {0,-10} {1}" -f $clip.format, $clip.file)
}
Write-Host "Manifest: $manifestPath"
