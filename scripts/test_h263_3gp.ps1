[CmdletBinding()]
param(
    [string]$Player
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$work = Join-Path $repo ".tmp\h263-3gp-test"
$source = Join-Path $work "source.mkv"
$profiles = @(
    "176x144",
    "256x144",
    "256x192",
    "320x180",
    "320x240"
)

if (-not (Test-Path -LiteralPath $ffmpeg)) {
    & (Join-Path $PSScriptRoot "bootstrap_ffmpeg.ps1")
}
if (-not $Player) {
    $outputDirectory = Join-Path $repo "build\h263-test"
    & (Join-Path $PSScriptRoot "build_windows_player.ps1") `
        -OutputDirectory $outputDirectory
    $Player = Join-Path $outputDirectory "hlvplay.exe"
}
if (-not (Test-Path -LiteralPath $Player)) {
    throw "Windows player is missing: $Player"
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
& $ffmpeg -y -hide_banner -loglevel error `
    -f lavfi -i "testsrc2=size=320x180:rate=15:duration=2" `
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=2" `
    -map 0:v:0 -map 1:a:0 -c:v ffv1 -c:a pcm_s16le $source
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the synthetic H.263 input."
}

foreach ($profile in $profiles) {
    $video = Join-Path $work "profile-$profile.3gp"
    & (Join-Path $PSScriptRoot "encode_h263_3gp.ps1") `
        -InputFile $source `
        -OutputFile $video `
        -Profile $profile `
        -FitMode Contain `
        -Fps 15 `
        -VideoBitrateKbps 256 `
        -VideoBufferKbps 512 `
        -Gop 15
    if ($LASTEXITCODE -ne 0) {
        throw "The $profile H.263/3GP encoder smoke test failed."
    }

    $quotedVideo = '"' + $video.Replace('"', '\"') + '"'
    $playerProcess = Start-Process -FilePath $Player `
        -ArgumentList "--check $quotedVideo" `
        -Wait -PassThru -WindowStyle Hidden
    if ($playerProcess.ExitCode -ne 0) {
        throw "The project decoder rejected the $profile smoke test."
    }
}

Write-Host "All H.263/3GP profile smoke tests passed."
