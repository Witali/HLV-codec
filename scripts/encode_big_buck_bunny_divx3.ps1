# Encode the approved Big Buck Bunny source to the first ESP32 DivX 3 profile.
[CmdletBinding()]
param(
    [ValidateRange(2, 31)]
    [int]$Quality = 4,

    [ValidateSet(12)]
    [int]$Fps = 12,

    [ValidateRange(1, 300)]
    [int]$Gop = 12,

    [ValidateRange(1, 16)]
    [int]$Threads = 8,

    [ValidateRange(0, 2147483647)]
    [int]$MaxFrames = 0,

    [string]$OutputFile,

    [string]$SelectionFile
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$sourceRelative = (
    "out\sources\big_buck_bunny_1080p_h264\" +
    "big_buck_bunny_1080p_h264.mov"
)

function Find-WorktreeFile {
    param([Parameter(Mandatory)][string]$RelativePath)

    $candidate = Join-Path $repo $RelativePath
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }
    foreach ($line in (git -C $repo worktree list --porcelain)) {
        if (-not $line.StartsWith("worktree ")) {
            continue
        }
        $candidate = Join-Path $line.Substring(9) $RelativePath
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    return Join-Path $repo $RelativePath
}

$source = Find-WorktreeFile $sourceRelative
$ffmpegRelative = "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobeRelative = "local_tools\ffmpeg\bin\ffprobe.exe"
$ffmpeg = Find-WorktreeFile $ffmpegRelative
$ffprobe = Find-WorktreeFile $ffprobeRelative

if (-not $OutputFile) {
    $OutputFile = Join-Path $repo (
        "out\BigBuckBunny_1080p_divx3_q{0}_{1}fps_320x240.avi" -f
        $Quality, $Fps
    )
}
if (-not $SelectionFile) {
    $SelectionFile = Join-Path $repo "out\play.txt"
}
$OutputFile = [IO.Path]::GetFullPath($OutputFile)
$SelectionFile = [IO.Path]::GetFullPath($SelectionFile)

if (-not (Test-Path -LiteralPath $source)) {
    throw "Required 1080p Big Buck Bunny source is missing: $sourceRelative"
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
    $ffmpeg = Join-Path $repo $ffmpegRelative
    $ffprobe = Join-Path $repo $ffprobeRelative
}
if (-not (Test-Path -LiteralPath $ffmpeg) -or
    -not (Test-Path -LiteralPath $ffprobe)) {
    throw "Repository-local FFmpeg is unavailable."
}

New-Item -ItemType Directory -Force -Path (
    Split-Path $OutputFile -Parent
) | Out-Null
New-Item -ItemType Directory -Force -Path (
    Split-Path $SelectionFile -Parent
) | Out-Null

# Match the established HLV/MJPEG PCM_U8 audio curve.
$audioConversion = "aformat=channel_layouts=mono,aresample=16000"
$audioLevelCurve = "acompressor=threshold=-20dB:ratio=1.6:" +
    "attack=0.01:release=250:knee=8:" +
    "link=maximum:detection=peak"
$audioPeakTargetDb = -0.1
$analysisFilter = "$audioConversion,$audioLevelCurve,astats=metadata=0:reset=0"

Write-Host "Measuring the established audio level curve..."
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
    throw "The established audio curve would require attenuation."
}
$curveMakeupText = $curveMakeupDb.ToString("0.000", $culture)
$audioFilter = (
    "$audioConversion,${audioLevelCurve}:makeup=${curveMakeupText}dB"
)

$arguments = @(
    "-y", "-hide_banner", "-loglevel", "warning", "-stats",
    "-i", $source,
    "-map", "0:v:0", "-map", "0:a:0",
    "-vf", (
        "fps=$Fps,scale=320:240:force_original_aspect_ratio=decrease:" +
        "flags=lanczos,pad=320:240:(ow-iw)/2:(oh-ih)/2:black," +
        "setsar=1,format=yuv420p"
    ),
    "-af", $audioFilter,
    "-fps_mode", "cfr",
    "-c:v", "msmpeg4",
    "-q:v", $Quality,
    "-g", $Gop,
    "-bf", "0",
    "-pix_fmt", "yuv420p",
    "-threads:v", $Threads,
    "-vtag", "DIV3",
    "-c:a", "pcm_u8",
    "-ar", "16000",
    "-ac", "1"
)
if ($MaxFrames) {
    $arguments += @("-frames:v", $MaxFrames, "-shortest")
}
$arguments += @("-f", "avi", $OutputFile)

Write-Host (
    "Encoding DivX 3 AVI: 320x240, $Fps fps, q=$Quality, GOP $Gop, " +
    "no B pictures, PCM_U8 mono 16 kHz..."
)
& $ffmpeg @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FFmpeg DivX 3 encoding failed with exit code $LASTEXITCODE."
}

$videoProbe = (
    & $ffprobe -v error -select_streams v:0 `
        -show_entries `
        stream=codec_name,codec_tag_string,width,height,r_frame_rate `
        -of json $OutputFile
) | ConvertFrom-Json
$video = $videoProbe.streams[0]
if ($LASTEXITCODE -ne 0 -or
    $video.codec_name -ne "msmpeg4v3" -or
    $video.codec_tag_string -ne "DIV3" -or
    $video.width -ne 320 -or $video.height -ne 240 -or
    $video.r_frame_rate -ne "$Fps/1") {
    throw "The output does not match the required DivX 3 video profile."
}

$audioProbe = (
    & $ffprobe -v error -select_streams a:0 `
        -show_entries stream=codec_name,channels,sample_rate `
        -of json $OutputFile
) | ConvertFrom-Json
$audio = $audioProbe.streams[0]
if ($LASTEXITCODE -ne 0 -or
    $audio.codec_name -ne "pcm_u8" -or
    $audio.channels -ne 1 -or
    $audio.sample_rate -ne "16000") {
    throw "The output does not match the required PCM_U8 audio profile."
}

$pictureTypes = & $ffprobe -v error -select_streams v:0 `
    -show_entries frame=pict_type -of csv=p=0 $OutputFile
if ($LASTEXITCODE -ne 0 -or
    ($pictureTypes | Where-Object { $_.Trim() -eq "B" })) {
    throw "The DivX 3 output contains B pictures or could not be inspected."
}

$packetSizes = & $ffprobe -v error -select_streams v:0 `
    -show_entries packet=size -of csv=p=0 $OutputFile
$maximumPacket = (
    $packetSizes |
        Where-Object { $_.Trim() } |
        ForEach-Object { [uint64]::Parse($_.Trim(), $culture) } |
        Measure-Object -Maximum
).Maximum
if ($LASTEXITCODE -ne 0 -or $null -eq $maximumPacket) {
    throw "Could not inspect DivX 3 video packets."
}
if ($maximumPacket -gt 96KB) {
    throw "Largest video packet exceeds the firmware limit: $maximumPacket bytes."
}

Write-Host "Decoding the complete AVI with FFmpeg..."
& $ffmpeg -v error -i $OutputFile -map 0:v:0 -map 0:a:0 -f null NUL
if ($LASTEXITCODE -ne 0) {
    throw "Full DivX 3/PCM_U8 validation failed."
}

$selectedName = [IO.Path]::GetFileName($OutputFile)
Set-Content -LiteralPath $SelectionFile -Value $selectedName -Encoding ascii
$result = Get-Item -LiteralPath $OutputFile
Write-Host ("Ready: {0} ({1:N0} bytes)" -f
    $result.FullName, $result.Length)
Write-Host ("Largest video packet: {0:N0} bytes" -f $maximumPacket)
Write-Host "Selection file: $SelectionFile -> $selectedName"
