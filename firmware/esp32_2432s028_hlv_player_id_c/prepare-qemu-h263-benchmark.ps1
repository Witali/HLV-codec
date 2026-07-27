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
        "out\VID_20260522_181611_h263_320x240_crop_512k.3gp"
    )
}
if (-not $InputFile -or -not (Test-Path -LiteralPath $InputFile)) {
    throw "320x240 H.263 benchmark input is missing."
}
$ffmpeg = Find-WorktreeFile "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Find-WorktreeFile "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not $ffmpeg -or -not $ffprobe) {
    throw "Repository-local FFmpeg is unavailable."
}
$output = Join-Path $project "main\qemu_h263_benchmark.3gp"
& $ffmpeg -hide_banner -loglevel error -y -i $InputFile `
    -map 0:v:0 -frames:v $Frames -c:v copy -an -f 3gp $output
if ($LASTEXITCODE -ne 0) {
    throw "Could not prepare the H.263 QEMU clip."
}
$probe = (
    & $ffprobe -v error -select_streams v:0 `
        -count_frames `
        -show_entries stream=codec_name,width,height,nb_read_frames `
        -of json $output
) | ConvertFrom-Json
$video = $probe.streams[0]
if ($LASTEXITCODE -ne 0 -or
    $video.codec_name -ne "h263" -or
    $video.width -ne 320 -or $video.height -ne 240 -or
    [int]$video.nb_read_frames -ne $Frames) {
    throw "Prepared QEMU clip does not match the 320x240 H.263 profile."
}
$size = (Get-Item -LiteralPath $output).Length
Write-Host "Prepared H.263 QEMU clip: $Frames frames, $size bytes"
