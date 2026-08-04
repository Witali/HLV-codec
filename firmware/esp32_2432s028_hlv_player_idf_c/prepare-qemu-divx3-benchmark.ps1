[CmdletBinding()]
param(
    [string]$InputFile = "",
    [ValidateRange(1, 120)]
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
        "out\DivX3\BigBuckBunny_320x240_12fps_DivX3_41dB.avi"
    )
}
if (-not $InputFile -or -not (Test-Path -LiteralPath $InputFile)) {
    throw "QVGA DivX 3 benchmark input is missing."
}
$ffmpeg = Find-WorktreeFile "local_tools\ffmpeg\bin\ffmpeg.exe"
$ffprobe = Find-WorktreeFile "local_tools\ffmpeg\bin\ffprobe.exe"
if (-not $ffmpeg -or -not $ffprobe) {
    throw "Repository-local FFmpeg is unavailable."
}
$output = Join-Path $project "main\qemu_divx3_benchmark.avi"
& $ffmpeg -hide_banner -loglevel error -y -i $InputFile `
    -map 0:v:0 -map 0:a:0? -frames:v $Frames `
    -c:v copy -c:a copy -shortest -f avi $output
if ($LASTEXITCODE -ne 0) {
    throw "Could not prepare the DivX 3 QEMU clip."
}
$probe = (
    & $ffprobe -v error -count_frames `
        -show_entries `
        stream=codec_type,codec_name,codec_tag_string,width,height,nb_read_frames,sample_rate,channels `
        -of json $output
) | ConvertFrom-Json
$video = @($probe.streams | Where-Object codec_type -eq "video")[0]
$audio = @($probe.streams | Where-Object codec_type -eq "audio")
if ($LASTEXITCODE -ne 0 -or
    $video.codec_name -ne "msmpeg4v3" -or
    $video.codec_tag_string -ne "DIV3" -or
    $video.width -ne 320 -or $video.height -ne 240 -or
    [int]$video.nb_read_frames -ne $Frames) {
    throw "Prepared QEMU clip does not match the QVGA DivX 3 profile."
}
$inputAudio = & $ffprobe -v error -select_streams a:0 `
    -show_entries stream=codec_name,sample_rate,channels -of json $InputFile |
    ConvertFrom-Json
$inputAudio = @($inputAudio.streams)
if ($inputAudio.Count -ne $audio.Count -or
    ($audio.Count -eq 1 -and
     ($audio[0].codec_name -ne $inputAudio[0].codec_name -or
      $audio[0].sample_rate -ne $inputAudio[0].sample_rate -or
      $audio[0].channels -ne $inputAudio[0].channels))) {
    throw "Prepared QEMU clip did not preserve the source audio profile."
}
$size = (Get-Item -LiteralPath $output).Length
$audioStatus = if ($audio.Count) {
    ", $($audio[0].codec_name) mono $($audio[0].sample_rate) Hz"
} else { ", no audio" }
Write-Host "Prepared DivX 3 QEMU clip: $Frames frames, $size bytes$audioStatus"
