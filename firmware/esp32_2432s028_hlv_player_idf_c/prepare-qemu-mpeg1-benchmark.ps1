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
        "out\MPEG1\BigBuckBunny_320x180_24fps_MPEG1_41dB.mpg"
    )
}
if (-not $InputFile -or -not (Test-Path -LiteralPath $InputFile)) {
    throw "320x180 MPEG-1 benchmark input is missing."
}
$ffmpeg = Find-WorktreeFile "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Find-WorktreeFile "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not $ffmpeg -or -not $ffprobe) {
    throw "Repository-local FFmpeg is unavailable."
}
$output = Join-Path $project "main\qemu_mpeg1_benchmark.mpg"
& $ffmpeg -hide_banner -loglevel error -y -i $InputFile `
    -map 0:v:0 -frames:v $Frames -c:v copy -an -f mpeg $output
if ($LASTEXITCODE -ne 0) {
    throw "Could not prepare the MPEG-1 QEMU clip."
}
$probe = (
    & $ffprobe -v error -select_streams v:0 `
        -count_frames `
        -show_entries stream=codec_name,width,height,nb_read_frames `
        -of json $output
) | ConvertFrom-Json
$video = $probe.streams[0]
if ($LASTEXITCODE -ne 0 -or
    $video.codec_name -ne "mpeg1video" -or
    $video.width -ne 320 -or $video.height -ne 180 -or
    [int]$video.nb_read_frames -ne $Frames) {
    throw "Prepared QEMU clip does not match the MPEG-1 profile."
}
$size = (Get-Item -LiteralPath $output).Length
Write-Host "Prepared MPEG-1 QEMU clip: $Frames frames, $size bytes"
