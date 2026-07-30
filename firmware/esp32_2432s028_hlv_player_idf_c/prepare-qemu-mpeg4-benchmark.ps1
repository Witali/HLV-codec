[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 90)]
    [int]$Frames = 60
)

$ErrorActionPreference = "Stop"
$project = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $project "..\..")).Path

function Find-WorktreeFile {
    param([Parameter(Mandatory)][string]$RelativePath)
    $candidate = Join-Path $repo $RelativePath
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    $common = (& git -C $repo rev-parse --git-common-dir).Trim()
    if (-not [IO.Path]::IsPathRooted($common)) {
        $common = [IO.Path]::GetFullPath((Join-Path $repo $common))
    }
    $primary = Split-Path -Parent $common
    $candidate = Join-Path $primary $RelativePath
    if (Test-Path -LiteralPath $candidate) {
        return (Resolve-Path -LiteralPath $candidate).Path
    }
    return $null
}

if (-not $InputFile) {
    $InputFile = Find-WorktreeFile (
        "out\MPEG4SP\VID_20260522_181611_320x240_30fps_" +
        "MPEG4SP_M4S2_q5.avi"
    )
}
if (-not $InputFile -or -not (Test-Path -LiteralPath $InputFile)) {
    throw "MPEG-4 Simple Profile benchmark input is missing."
}
$ffmpeg = Find-WorktreeFile "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Find-WorktreeFile "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not $ffmpeg -or -not $ffprobe) {
    throw "Repository-local FFmpeg is unavailable."
}
$output = Join-Path $project "main\qemu_mpeg4_benchmark.avi"
& $ffmpeg -hide_banner -loglevel error -y -i $InputFile `
    -map 0:v:0 -frames:v $Frames -c:v copy -an -f avi $output
if ($LASTEXITCODE -ne 0) {
    throw "Could not prepare the MPEG-4 SP QEMU clip."
}
$probe = (
    & $ffprobe -v error -select_streams v:0 -count_frames `
        -show_entries `
        stream=codec_name,profile,codec_tag_string,width,height,has_b_frames,nb_read_frames `
        -of json $output
) | ConvertFrom-Json
$video = $probe.streams[0]
if ($LASTEXITCODE -ne 0 -or
    $video.codec_name -ne "mpeg4" -or
    $video.profile -ne "Simple Profile" -or
    $video.codec_tag_string -ne "M4S2" -or
    $video.width -ne 320 -or $video.height -ne 240 -or
    $video.has_b_frames -ne 0 -or
    [int]$video.nb_read_frames -ne $Frames) {
    throw "Prepared QEMU clip does not match the MPEG-4 SP/M4S2 profile."
}
$packets = (
    & $ffprobe -v error -select_streams v:0 `
        -show_entries packet=size -show_packets -of json $output
) | ConvertFrom-Json
$maximumPacket = (@($packets.packets |
    ForEach-Object { [int]$_.size }) |
    Measure-Object -Maximum).Maximum
if ($maximumPacket -le 4096) {
    throw "Prepared QEMU clip has no packet larger than the 4 KiB refill."
}
$size = (Get-Item -LiteralPath $output).Length
Write-Host (
    "Prepared MPEG-4 SP QEMU clip: $Frames frames, $size bytes, " +
    "max packet $maximumPacket bytes"
)
