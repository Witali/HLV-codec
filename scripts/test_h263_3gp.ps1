[CmdletBinding()]
param(
    [string]$Player
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$ffmpeg = Join-Path $repo "local_tools\ffmpeg\bin\ffmpeg.exe"
$work = Join-Path $repo ".tmp\h263-3gp-test"
$source = Join-Path $work "source.mkv"
$video = Join-Path $work "qcif.3gp"

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
    -c:v ffv1 $source
if ($LASTEXITCODE -ne 0) {
    throw "Could not generate the synthetic H.263 input."
}

& (Join-Path $PSScriptRoot "encode_h263_3gp.ps1") `
    -InputFile $source `
    -OutputFile $video `
    -Fps 15 `
    -VideoBitrateKbps 128 `
    -Gop 15
if ($LASTEXITCODE -ne 0) {
    throw "The H.263/3GP encoder smoke test failed."
}

& $Player --check $video
if ($LASTEXITCODE -ne 0) {
    throw "The project H.263/3GP decoder rejected the smoke test."
}

Write-Host "H.263/3GP smoke test passed."
