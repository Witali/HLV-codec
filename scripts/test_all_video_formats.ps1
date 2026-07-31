#requires -Version 7.4

<#
.SYNOPSIS
Generates and fully decodes a short picture-rich clip in every production
ESP32 video format.
#>
[CmdletBinding()]
param(
    [ValidateRange(8, 300)]
    [int]$Frames = 60,

    [ValidateRange(1, 30)]
    [int]$Fps = 30,

    [string]$OutputDirectory = (
        Join-Path (Split-Path $PSScriptRoot -Parent) `
            ".tmp\all-video-format-tests"
    ),

    [switch]$SkipBuild,
    [switch]$SkipEncode
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Join-Path $repo "local_tools\ffmpeg\bin\ffprobe.exe"
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$corpus = Join-Path $OutputDirectory "corpus"
$build = Join-Path $OutputDirectory "build"
$source = Join-Path $corpus "sources\VideoFormatRegression.mkv"

function Format-Rate {
    param([Parameter(Mandatory)][double]$Value)
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $rounded = [Math]::Round($Value)
    if ([Math]::Abs($Value - $rounded) -lt 0.0005) {
        return $rounded.ToString("0", $culture)
    }
    return $Value.ToString("0.###", $culture).Replace(".", "p")
}

function Get-DecodedFrameCount {
    param([Parameter(Mandatory)][string]$File)
    $probeText = & $ffprobe -v error -select_streams v:0 `
        -count_frames -show_entries stream=nb_read_frames `
        -of json $File
    if ($LASTEXITCODE -ne 0) {
        throw "FFprobe could not count decoded frames in $File."
    }
    $streams = @(($probeText | ConvertFrom-Json).streams)
    if ($streams.Count -ne 1 -or
        -not $streams[0].nb_read_frames) {
        throw "No decoded video frame count was reported for $File."
    }
    return [int]$streams[0].nb_read_frames
}

function Assert-FfmpegFullDecode {
    param(
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][int]$ExpectedFrames
    )
    & $ffmpeg -v error -xerror -i $File `
        -map 0:v:0 -map "0:a:0?" -f null NUL
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg did not fully decode $File."
    }
    $actualFrames = Get-DecodedFrameCount -File $File
    if ($actualFrames -ne $ExpectedFrames) {
        throw (
            "Decoded frame count mismatch for $File`: " +
            "expected $ExpectedFrames, got $actualFrames."
        )
    }
}

function Invoke-PlayerCheck {
    param(
        [Parameter(Mandatory)][string]$Player,
        [Parameter(Mandatory)][string]$File,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][int]$ExpectedFrames
    )
    $result = & cmd.exe /d /c (
        '"{0}" --check "{1}"' -f $Player, $File
    )
    if ($LASTEXITCODE -ne 0) {
        throw "The project decoder rejected $File."
    }
    $text = $result -join "`n"
    $pattern = (
        [regex]::Escape($Label) +
        " check OK: $ExpectedFrames frames, (\d+) audio bytes, " +
        "checksum ([0-9a-fA-F]{16})"
    )
    if ($text -notmatch $pattern) {
        throw "Unexpected project decoder result for $File`: $text"
    }
    if ([uint64]$Matches[1] -eq 0) {
        throw "The project decoder found no audio payload in $File."
    }
    return [pscustomobject]@{
        Frames = $ExpectedFrames
        AudioBytes = [uint64]$Matches[1]
        Checksum = $Matches[2].ToLowerInvariant()
    }
}

function Build-Divx3StreamTest {
    param([Parameter(Mandatory)][string]$Destination)
    $vswhere =
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath | Select-Object -First 1
    if (-not $installation) {
        throw "Visual Studio C++ tools are missing."
    }
    $devcmd = Join-Path $installation "Common7\Tools\VsDevCmd.bat"
    $include = Join-Path $repo "codecs\divx3\include"
    $compactInclude = Join-Path $repo "codecs\common\include"
    $testSource = Join-Path $repo "codecs\divx3\tests\test_stream_decode.c"
    $decoderSource = Join-Path $repo "codecs\divx3\src\divx3_decode.c"
    $aviSource = Join-Path $repo "codecs\divx3\src\divx3_avi.c"
    $command = (
        'call "{0}" -no_logo -arch=x64 && cd /d "{1}" && ' +
        'cl /nologo /O2 /W4 /TC /D_CRT_SECURE_NO_WARNINGS ' +
        '/I"{2}" /I"{3}" "{4}" "{5}" "{6}" /Fe:"{7}"'
    ) -f $devcmd, $build, $include, $compactInclude, $testSource,
        $decoderSource, $aviSource, $Destination
    & cmd.exe /d /c $command | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC failed while building the DivX 3 stream test."
    }
}

if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
New-Item -ItemType Directory -Force -Path $corpus | Out-Null
New-Item -ItemType Directory -Force -Path $build | Out-Null

$rate = Format-Rate -Value $Fps
$divxFrames = [int][Math]::Ceiling($Frames / 2.0)
$divxRate = Format-Rate -Value ($Fps / 2.0)
$hlv = Join-Path $corpus (
    "HLV\VideoFormatRegression_320x240_${rate}fps_" +
    "HLVv15_adaptive35-42dB.hlv"
)
$mjpeg = Join-Path $corpus (
    "MJPEG\VideoFormatRegression_320x240_${rate}fps_MJPEG_q3.avi"
)
$divx3 = Join-Path $corpus (
    "DivX3\VideoFormatRegression_320x240_${divxRate}fps_DivX3_q3.avi"
)
$mpeg1 = Join-Path $corpus (
    "MPEG1\VideoFormatRegression_320x240_${rate}fps_MPEG1_q3.mpg"
)
$h263 = Join-Path $corpus (
    "H263\VideoFormatRegression_352x288_${rate}fps_H263_CIF_q6.avi"
)
$mpeg4 = Join-Path $corpus (
    "MPEG4SP\VideoFormatRegression_320x240_${rate}fps_" +
    "MPEG4SP_35dB.avi"
)

if (-not $SkipEncode) {
    & (Join-Path $PSScriptRoot "generate_all_video_formats.ps1") `
        -OutputRoot $corpus -Frames $Frames -Fps $Fps `
        -BpvTargetPsnrDb 35 -BpvDevice Auto -Force
}
elseif (-not (Test-Path -LiteralPath $source)) {
    throw "-SkipEncode requires an existing generated source: $source"
}
if ((Get-DecodedFrameCount -File $source) -ne $Frames) {
    throw "The visual regression source is incomplete."
}

$bpvCandidates = @(
    Get-ChildItem -LiteralPath (Join-Path $corpus "BPV") `
        -Filter "VideoFormatRegression_320x240_${rate}fps_BPVv6_*.bpv1"
)
if ($bpvCandidates.Count -ne 1) {
    throw "Expected exactly one production-named BPV v6 output."
}
$bpv = $bpvCandidates[0].FullName

$expected = @(
    [pscustomobject]@{ Name = "HLV v15"; Path = $hlv; Frames = $Frames },
    [pscustomobject]@{ Name = "BPV v6"; Path = $bpv; Frames = $Frames },
    [pscustomobject]@{ Name = "MJPEG"; Path = $mjpeg; Frames = $Frames },
    [pscustomobject]@{
        Name = "DivX 3"; Path = $divx3; Frames = $divxFrames
    },
    [pscustomobject]@{ Name = "MPEG-1"; Path = $mpeg1; Frames = $Frames },
    [pscustomobject]@{ Name = "H.263"; Path = $h263; Frames = $Frames },
    [pscustomobject]@{
        Name = "MPEG-4 SP"; Path = $mpeg4; Frames = $Frames
    }
)
foreach ($entry in $expected) {
    if (-not (Test-Path -LiteralPath $entry.Path)) {
        throw "Missing encoded regression clip: $($entry.Path)"
    }
    if ($entry.Name -notin @("HLV v15", "BPV v6")) {
        Assert-FfmpegFullDecode `
            -File $entry.Path -ExpectedFrames $entry.Frames
    }
}

$player = Join-Path $build "hlvplay.exe"
if (-not $SkipBuild -or -not (Test-Path -LiteralPath $player)) {
    & (Join-Path $PSScriptRoot "build_windows_player.ps1") `
        -OutputDirectory $build
}
if (-not (Test-Path -LiteralPath $player)) {
    throw "The native project decoder is unavailable."
}

$checks = [ordered]@{}
$checks["HLV v15"] = Invoke-PlayerCheck `
    -Player $player -File $hlv -Label "HLV" -ExpectedFrames $Frames
$checks["BPV v6"] = Invoke-PlayerCheck `
    -Player $player -File $bpv -Label "BPV1" -ExpectedFrames $Frames
$checks["MPEG-1"] = Invoke-PlayerCheck `
    -Player $player -File $mpeg1 -Label "MPEG-1" -ExpectedFrames $Frames
$checks["H.263"] = Invoke-PlayerCheck `
    -Player $player -File $h263 -Label "H.263/AVI" `
    -ExpectedFrames $Frames
$checks["MPEG-4 SP"] = Invoke-PlayerCheck `
    -Player $player -File $mpeg4 -Label "MPEG-4 SP/AVI" `
    -ExpectedFrames $Frames

$hlvSimulatorProject = Join-Path $repo (
    "firmware\esp32_2432s028_hlv_player_idf_c\simulator"
)
$hlvSimulator = Join-Path $hlvSimulatorProject (
    "build\hlv_esp32_sim.exe"
)
if (-not $SkipBuild -or -not (Test-Path -LiteralPath $hlvSimulator)) {
    & (Join-Path $hlvSimulatorProject "build.ps1") `
        -Optimization O3 -Architecture x64 -BitReaderBits 32 `
        -DecoderStats 0
}
$hlvSimulatorResult = & $hlvSimulator $hlv 1
$hlvSimulatorText = $hlvSimulatorResult -join "`n"
if ($LASTEXITCODE -ne 0 -or
    $hlvSimulatorText -notmatch
        "Compact/expanded reconstruction: bit exact" -or
    $hlvSimulatorText -notmatch
        "257-byte refill reconstruction: bit exact" -or
    $hlvSimulatorText -notmatch
        "Single-reference segmented/refill reconstruction: bit exact") {
    throw "The ESP32 compact HLV decoder did not match the exact decoder."
}
$checks["HLV v15"] | Add-Member `
    -NotePropertyName Esp32CompactReference `
    -NotePropertyValue "bit exact (segmented, refill, single-reference)"

$divxTest = Join-Path $build "test_divx3_stream_decode.exe"
if (-not $SkipBuild -or -not (Test-Path -LiteralPath $divxTest)) {
    Build-Divx3StreamTest -Destination $divxTest
}
$divxResult = & $divxTest $divx3
if ($LASTEXITCODE -ne 0 -or
    ($divxResult -join "`n") -notmatch
        "frames=$divxFrames .*checksums=identical") {
    throw "The project DivX 3 streaming decoder did not finish the clip."
}

$manifest = [ordered]@{
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    source = $source
    sourceFrames = $Frames
    sourceFps = $Fps
    clips = @(
        foreach ($entry in $expected) {
            [ordered]@{
                format = $entry.Name
                file = [IO.Path]::GetFileName($entry.Path)
                frames = $entry.Frames
                bytes = (Get-Item -LiteralPath $entry.Path).Length
                projectDecoder = if ($checks.Contains($entry.Name)) {
                    $checks[$entry.Name]
                }
                elseif ($entry.Name -eq "DivX 3") {
                    [ordered]@{
                        Frames = $divxFrames
                        Result = $divxResult -join "`n"
                    }
                }
                else {
                    "ESP/QEMU esp_new_jpeg acceptance required"
                }
            }
        }
    )
}
$manifestPath = Join-Path $OutputDirectory "manifest.json"
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host ""
Write-Host "All production video-format clips decoded completely:"
foreach ($entry in $expected) {
    Write-Host (
        "  {0,-10} {1,3} frames  {2}" -f
        $entry.Name, $entry.Frames, [IO.Path]::GetFileName($entry.Path)
    )
}
Write-Host "Manifest: $manifestPath"
